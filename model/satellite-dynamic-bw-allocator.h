#ifndef SATELLITE_DYNAMIC_BW_ALLOCATOR_H
#define SATELLITE_DYNAMIC_BW_ALLOCATOR_H

#include "satellite-fwd-link-scheduler.h"

#include "ns3/nstime.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <map>
#include <stdint.h>

namespace ns3
{

/**
 * Demand-proportional forward-link bandwidth allocator for a single beam.
 *
 * Fires every superframe period, reads the GW LLC queue depth for a given
 * beam, updates an EWMA demand estimate, and adjusts the forward-link
 * scheduler's carrier bandwidth proportionally between a configured minimum
 * and maximum.
 */
class SatDynamicBwAllocator : public Object
{
  public:
    static TypeId GetTypeId(void);
    SatDynamicBwAllocator();
    virtual ~SatDynamicBwAllocator();

    /**
     * Start the periodic allocation loop.
     * @param scheduler  The GW forward-link scheduler to control.
     * @param beamId     The beam whose queue depth drives allocation.
     * @param interval   How often to re-evaluate (should match superframe duration).
     * @param minBwHz    Minimum carrier bandwidth to allocate [Hz].
     * @param maxBwHz    Maximum carrier bandwidth to allocate [Hz].
     */
    void Start(Ptr<SatFwdLinkScheduler> scheduler,
               uint32_t beamId,
               Time interval,
               double minBwHz,
               double maxBwHz);

    /** Returns the time-averaged allocated bandwidth [Hz] since Start(). */
    double GetMeanBandwidthHz() const;

    /** Prints mean allocated bandwidth to std::cout for easy comparison. */
    void PrintMeanBandwidth() const;

  private:
    void UpdateAllocation();

    Ptr<SatFwdLinkScheduler> m_scheduler;
    uint32_t m_beamId;
    Time m_interval;
    double m_minBwHz;
    double m_maxBwHz;

    double m_smoothedDemand;
    uint32_t m_prevQueueBytes;

    // EWMA smoothing factor
    double m_alpha;
    // Bytes-per-interval that maps to full (max) bandwidth
    double m_demandSaturationBytes;

    uint64_t m_updateCount;
    double m_bwAccumulator;
};

} // namespace ns3

#endif /* SATELLITE_DYNAMIC_BW_ALLOCATOR_H */
