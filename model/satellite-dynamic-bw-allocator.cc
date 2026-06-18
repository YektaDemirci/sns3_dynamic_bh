#include "satellite-dynamic-bw-allocator.h"

#include "satellite-llc.h"
#include "satellite-orbiter-net-device.h"
#include "satellite-orbiter-user-llc.h"
#include "satellite-topology.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/singleton.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SatDynamicBwAllocator");

NS_OBJECT_ENSURE_REGISTERED(SatDynamicBwAllocator);

TypeId
SatDynamicBwAllocator::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::SatDynamicBwAllocator")
                            .SetParent<Object>()
                            .AddConstructor<SatDynamicBwAllocator>();
    return tid;
}

SatDynamicBwAllocator::SatDynamicBwAllocator()
    : m_beamId(0),
      m_interval(MilliSeconds(1)),
      m_minBwHz(50e6),
      m_maxBwHz(500e6),
      m_smoothedDemand(0.0),
      m_prevQueueBytes(0),
      m_alpha(0.3),
      m_demandSaturationBytes(1e6),
      m_updateCount(0),
      m_bwAccumulator(0.0)
{
}

SatDynamicBwAllocator::~SatDynamicBwAllocator()
{
}

void
SatDynamicBwAllocator::Start(Ptr<SatFwdLinkScheduler> scheduler,
                              uint32_t beamId,
                              Time interval,
                              double minBwHz,
                              double maxBwHz)
{
    m_scheduler = scheduler;
    m_beamId = beamId;
    m_interval = interval;
    m_minBwHz = minBwHz;
    m_maxBwHz = maxBwHz;

    m_demandSaturationBytes = maxBwHz * interval.GetSeconds() / 8.0;

    Simulator::Schedule(m_interval, &SatDynamicBwAllocator::UpdateAllocation, this);
}

void
SatDynamicBwAllocator::UpdateAllocation()
{
    // Read the orbiter user-link LLC queue for this beam.
    // This queue sits between the satellite and the UTs — it fills when the
    // user-link scheduler can't drain fast enough, so it's the right signal.
    uint32_t qNow = 0;
    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
    for (uint32_t satId = 0; satId < topology->GetNOrbiterNodes(); ++satId)
    {
        Ptr<Node> satNode = topology->GetOrbiterNode(satId);
        Ptr<SatOrbiterUserLlc> uLlc = topology->GetOrbiterUserLlc(satNode, m_beamId);
        if (uLlc)
        {
            qNow += DynamicCast<SatLlc>(uLlc)->GetNBytesInQueue();
        }
    }

    // EWMA demand estimate
    double demandSample = static_cast<double>(qNow);
    m_smoothedDemand = m_alpha * demandSample + (1.0 - m_alpha) * m_smoothedDemand;
    m_prevQueueBytes = qNow;

    // Map demand → bandwidth linearly between min and max
    double fraction = (m_demandSaturationBytes > 0.0)
                          ? std::min(m_smoothedDemand / m_demandSaturationBytes, 1.0)
                          : 1.0;
    double newBw = m_minBwHz + fraction * (m_maxBwHz - m_minBwHz);

    m_bwAccumulator += newBw;
    ++m_updateCount;
    double meanBw = m_bwAccumulator / static_cast<double>(m_updateCount);

    NS_LOG_INFO("t=" << Simulator::Now().GetSeconds()
                     << "s  beam=" << m_beamId
                     << "  satQueueBytes=" << qNow
                     << "  smoothed=" << m_smoothedDemand
                     << "  bwHz=" << newBw
                     << "  meanBwHz=" << meanBw);

    m_scheduler->SetCarrierBandwidthHz(newBw);

    Simulator::Schedule(m_interval, &SatDynamicBwAllocator::UpdateAllocation, this);
}

double
SatDynamicBwAllocator::GetMeanBandwidthHz() const
{
    if (m_updateCount == 0)
    {
        return 0.0;
    }
    return m_bwAccumulator / static_cast<double>(m_updateCount);
}

void
SatDynamicBwAllocator::PrintMeanBandwidth() const
{
    double mean = GetMeanBandwidthHz();
    std::cout << "DynamicBwAllocator beam=" << m_beamId
              << "  mean allocated BW = " << mean / 1e6 << " MHz"
              << "  (over " << m_updateCount << " superframes)"
              << std::endl;
}

} // namespace ns3
