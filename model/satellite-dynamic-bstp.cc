#include "satellite-dynamic-bstp.h"

#include "ns3/log.h"
#include "ns3/satellite-llc.h"
#include "ns3/satellite-net-device.h"
#include "ns3/satellite-orbiter-net-device.h"
#include "ns3/satellite-gw-llc.h"
#include "ns3/satellite-orbiter-user-llc.h"
#include "ns3/satellite-topology.h"
#include "ns3/simulator.h"
#include "ns3/singleton.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iomanip>

// POSIX headers for fork/exec/pipe
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SatDynamicBstp");
NS_OBJECT_ENSURE_REGISTERED(SatDynamicBstp);

TypeId
SatDynamicBstp::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::SatDynamicBstp")
                            .SetParent<Object>()
                            .AddConstructor<SatDynamicBstp>();
    return tid;
}

SatDynamicBstp::SatDynamicBstp()
    : m_planSuperframes(s_planSuperframes > 0 ? s_planSuperframes : 15),
      m_forecastWindow(24),
      m_planIndex(0),
      m_planCount(0),
      m_warmupPlans(24),
      m_gamma(1.0),
      m_lastPrintedSec(-1),
      m_farPid(-1),
      m_farWriteFd(-1),
      m_farReadFd(-1)
{
}

SatDynamicBstp::~SatDynamicBstp()
{
    if (m_planLog.is_open())
        m_planLog.close();
    for (auto& kv : m_beamDemandLogs)
        if (kv.second.is_open()) kv.second.close();
    if (m_farWriteFd >= 0) { close(m_farWriteFd); m_farWriteFd = -1; }
    if (m_farReadFd  >= 0) { close(m_farReadFd);  m_farReadFd  = -1; }
    if (m_farPid > 0)
    {
        waitpid(m_farPid, nullptr, WNOHANG);
        m_farPid = -1;
    }
}

void
SatDynamicBstp::StartFarDaemon()
{
    // Locate far.py next to this binary or via a known path.
    // Try the examples directory relative to the ns-3 root derived from argv[0].
    // Fall back to a hard-coded path so it always works in the dev tree.
    const char* scriptEnv = std::getenv("FAR_PY_PATH");
    std::string script;
    bool useFgn = !s_hurstParams.empty();
    if (scriptEnv)
    {
        script = scriptEnv;
    }
    else
    {
        const char* pyName = useFgn ? "fgn.py" : "far.py";
        const std::string rel1 = std::string("contrib/satellite/examples/") + pyName;
        const std::string rel2 = std::string("../contrib/satellite/examples/") + pyName;
        const char* candidates[] = { rel1.c_str(), rel2.c_str() };
        for (const char* c : candidates)
        {
            if (access(c, F_OK) == 0) { script = c; break; }
        }
    }
    if (script.empty())
    {
        NS_LOG_WARN("SatDynamicBstp: forecast script not found — forecast will use mean");
        return;
    }

    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0 || pipe(fromChild) != 0)
    {
        NS_LOG_WARN("SatDynamicBstp: pipe() failed: " << strerror(errno)
                    << " — forecast will use mean");
        return;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        NS_LOG_WARN("SatDynamicBstp: fork() failed: " << strerror(errno)
                    << " — forecast will use mean");
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);
        return;
    }

    if (pid == 0)
    {
        // Child: wire pipes and exec forecast daemon
        dup2(toChild[0],   STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);

        if (!s_hurstParams.empty())
        {
            // fgn.py: build argv with one --hurstNN=X arg per beam
            std::vector<std::string> argStrs;
            for (const auto& kv : s_hurstParams)
            {
                std::ostringstream a;
                a << "--hurst" << kv.first << "=" << std::fixed << std::setprecision(6) << kv.second;
                argStrs.push_back(a.str());
            }
            std::vector<const char*> argv;
            argv.push_back("python3");
            argv.push_back(script.c_str());
            for (const auto& s : argStrs) argv.push_back(s.c_str());
            argv.push_back(nullptr);
            execvp("python3", const_cast<char* const*>(argv.data()));
        }
        else
        {
            execlp("python3", "python3", script.c_str(), nullptr);
        }
        _exit(127);
    }

    // Parent: keep write end to child stdin, read end from child stdout
    close(toChild[0]);
    close(fromChild[1]);
    m_farWriteFd = toChild[1];
    m_farReadFd  = fromChild[0];
    m_farPid     = pid;
    NS_LOG_INFO("SatDynamicBstp: launched " << (useFgn ? "fgn.py" : "far.py") << " (pid=" << pid << ")");
}

double
SatDynamicBstp::QueryFar(uint32_t beamId, const std::deque<double>& samples)
{
    double mean_val = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();

    if (m_farPid < 0)
    {
        StartFarDaemon();
        if (m_farPid < 0)
        {
            return mean_val; // subprocess unavailable
        }
    }

    // Build CSV line: prepend beam ID when using fgn.py (hurst params set)
    std::ostringstream oss;
    if (!s_hurstParams.empty())
        oss << beamId << ',';
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (i) oss << ',';
        oss << std::fixed << std::setprecision(6) << samples[i];
    }
    oss << '\n';
    std::string csv = oss.str();

    // Write to far.py stdin
    ssize_t written = write(m_farWriteFd, csv.c_str(), csv.size());
    if (written != static_cast<ssize_t>(csv.size()))
    {
        NS_LOG_WARN("SatDynamicBstp: write to far.py failed — using mean");
        return mean_val;
    }

    // Read one line back (forecast)
    std::string response;
    char ch;
    while (true)
    {
        ssize_t n = read(m_farReadFd, &ch, 1);
        if (n <= 0) { NS_LOG_WARN("SatDynamicBstp: read from far.py failed — using mean"); return mean_val; }
        if (ch == '\n') break;
        response += ch;
    }

    try
    {
        return std::max(0.0, std::stod(response));
    }
    catch (...)
    {
        NS_LOG_WARN("SatDynamicBstp: bad response from far.py: '" << response << "' — using mean");
        return mean_val;
    }
}

std::map<uint32_t, uint32_t> SatDynamicBstp::s_fixedSlots;
std::map<uint32_t, uint32_t> SatDynamicBstp::s_warmupSlots;
uint32_t SatDynamicBstp::s_planSuperframes = 0;
std::map<uint32_t, double>   SatDynamicBstp::s_hurstParams;

void
SatDynamicBstp::SetPlanSuperframes(uint32_t n)
{
    s_planSuperframes = n;
}

void
SatDynamicBstp::SetFixedSlots(const std::map<uint32_t, uint32_t>& beamSlots)
{
    s_fixedSlots = beamSlots;
}

void
SatDynamicBstp::SetWarmupSlots(const std::map<uint32_t, uint32_t>& beamSlots)
{
    s_warmupSlots = beamSlots;
}

void
SatDynamicBstp::SetHurstParams(const std::map<uint32_t, double>& hurstParams)
{
    s_hurstParams = hurstParams;
}

void
SatDynamicBstp::AddEnabledBeamInfo(uint32_t beamId,
                                   uint32_t userFreqId,
                                   uint32_t feederFreqId,
                                   uint32_t gwId)
{
    if (std::find(m_enabledBeams.begin(), m_enabledBeams.end(), beamId) == m_enabledBeams.end())
    {
        m_enabledBeams.push_back(beamId);
        m_beamGwId[beamId]  = gwId;
        m_gwQueueHistory[beamId] = std::deque<double>();

        // Resolve the satellite ID that serves this beam (needed for GW LLC lookup).
        // Store the first match found; in a single-satellite scenario this is always 0.
        Ptr<SatTopology> topology = Singleton<SatTopology>::Get();
        for (uint32_t satId = 0; satId < topology->GetNOrbiterNodes(); ++satId)
        {
            Ptr<Node> satNode = topology->GetOrbiterNode(satId);
            if (topology->GetOrbiterUserLlc(satNode, beamId))
            {
                m_beamSatId[beamId] = satId;
                break;
            }
        }
        if (m_beamSatId.find(beamId) == m_beamSatId.end())
        {
            m_beamSatId[beamId] = 0;
        }
    }
}

std::map<uint32_t, uint64_t>
SatDynamicBstp::ReadAndResetArrivalBytes()
{
    std::map<uint32_t, uint64_t> arrivalBytes;
    std::map<uint32_t, uint64_t> satArrivalBytes;
    for (uint32_t bid : m_enabledBeams)
    {
        arrivalBytes[bid]    = 0;
        satArrivalBytes[bid] = 0;
    }

    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    // GW LLC: bytes enqueued from IP layer before feeder scheduler
    for (uint32_t bid : m_enabledBeams)
    {
        Ptr<Node> gw = topology->GetGwFromBeam(bid);
        if (!gw)
            continue;
        uint32_t satId = m_beamSatId.count(bid) ? m_beamSatId.at(bid) : 0;
        Ptr<SatGwLlc> gwLlc = topology->GetGwLlcSafe(gw, satId, bid);
        if (gwLlc)
            arrivalBytes[bid] += gwLlc->GetAndResetArrivalBytes();
    }

    // SAT LLC (orbiter user side): bytes arriving from feeder after feeder scheduler
    for (uint32_t bid : m_enabledBeams)
    {
        uint32_t satId = m_beamSatId.count(bid) ? m_beamSatId.at(bid) : 0;
        Ptr<Node> satNode = topology->GetOrbiterNode(satId);
        if (!satNode)
            continue;
        Ptr<SatOrbiterUserLlc> satLlc = topology->GetOrbiterUserLlc(satNode, bid);
        if (satLlc)
            satArrivalBytes[bid] += satLlc->GetAndResetArrivalBytes();
    }

    m_satArrivalBytes = satArrivalBytes;

    return satArrivalBytes;
}

void
SatDynamicBstp::RegeneratePlan()
{
    NS_LOG_FUNCTION(this);

    // ── Fixed scheduling shortcut ─────────────────────────────────────────
    if (!s_fixedSlots.empty())
    {
        // Still drain arrival counters so they don't overflow, but skip ARFIMA.
        ReadAndResetArrivalBytes();

        uint32_t nBeams = m_enabledBeams.size();
        std::vector<uint32_t> slots(nBeams);
        for (uint32_t k = 0; k < nBeams; ++k)
        {
            auto it = s_fixedSlots.find(m_enabledBeams[k]);
            slots[k] = (it != s_fixedSlots.end()) ? it->second : 0;
        }

        m_plan.clear();
        m_plan.reserve(m_planSuperframes);
        std::vector<uint32_t> rem = slots;
        bool anyLeft = true;
        while (anyLeft)
        {
            anyLeft = false;
            for (uint32_t k = 0; k < nBeams; ++k)
                if (rem[k] > 0) { m_plan.push_back(m_enabledBeams[k]); rem[k]--; anyLeft = true; }
        }

        int64_t tMsFixed = Simulator::Now().GetMilliSeconds();
        int64_t tSecFixed = tMsFixed / 1000;
        if (tSecFixed != m_lastPrintedSec && tMsFixed % 1000 < static_cast<int64_t>(m_planSuperframes) + 1)
        {
            m_lastPrintedSec = tSecFixed;
            std::cout << "[" << tSecFixed << "s]  slots (fixed):";
            for (uint32_t k = 0; k < nBeams; ++k)
                std::cout << "  b" << m_enabledBeams[k] << "=" << slots[k];
            std::cout << std::endl;
        }
        return;
    }

    // ── Step 1: read UT-count-scaled SAT LLC arrival bytes per beam ──────
    // Raw SAT LLC arrivals are compressed by feeder equalization; scaling by
    // total_UTs/beam_UTs recovers a demand signal proportional to user count.
    std::map<uint32_t, uint64_t> gwQueue = ReadAndResetArrivalBytes();

    // ── Per-beam demand logs ──────────────────────────────────────────────
    int64_t tMsEarly = Simulator::Now().GetMilliSeconds();
    for (uint32_t bid : m_enabledBeams)
    {
        auto& f = m_beamDemandLogs[bid];
        if (!f.is_open())
        {
            f.open("beam" + std::to_string(bid) + ".csv", std::ios::out | std::ios::trunc);
            f << "time_ms,demand_bytes\n";
        }
        f << tMsEarly << "," << gwQueue[bid] << "\n";
    }

    // Open the plan log CSV on first call
    if (!m_planLog.is_open())
    {
        m_planLog.open("bh_plan_log.csv", std::ios::out | std::ios::trunc);
        m_planLog << "time_ms,beam_id,actual_bytes,sat_arrivals,forecast_bytes,weight,slots,prev_forecast\n";
    }

    bool prevWasArfima = (m_planCount > m_warmupPlans); // m_planCount not yet incremented

    // ── Step 2: append arrival sample to ARFIMA history ──────────────────
    for (uint32_t bid : m_enabledBeams)
    {
        auto& hist = m_gwQueueHistory[bid];
        hist.push_back(static_cast<double>(gwQueue[bid]));
        if (hist.size() > m_forecastWindow)
            hist.pop_front();
    }

    // ── Step 3: compute weights ───────────────────────────────────────────
    // w_i = arfima_forecast_of_arrivals_i
    //
    // forecast_i: ARFIMA(2,0,0) 1-step-ahead prediction of arrival bytes,
    //             trained on last 24 per-period arrival samples (15 ms each).
    // During warmup the forecast history is cold, so we use the current
    // period's raw arrivals as a direct proxy instead.
    m_planCount++;
    bool inWarmup = (m_planCount <= m_warmupPlans);

    std::map<uint32_t, double> weights;
    double totalWeight = 0.0;

    std::map<uint32_t, double> forecasts;

    for (uint32_t bid : m_enabledBeams)
    {
        double forecast = 0.0;

        if (!inWarmup)
        {
            const auto& hist = m_gwQueueHistory[bid];
            if (hist.size() >= 3)
                forecast = QueryFar(bid, hist);
            else if (!hist.empty())
                forecast = std::accumulate(hist.begin(), hist.end(), 0.0) / hist.size();
        }
        else
        {
            // Warmup: use raw arrivals this period as a direct demand proxy
            forecast = static_cast<double>(gwQueue[bid]);
        }

        forecasts[bid] = forecast;
        double w = forecast;
        weights[bid]  = w;
        totalWeight  += w;
    }

    // ── Step 4: allocate m_planSuperframes slots proportional to weights ──
    uint32_t nBeams = m_enabledBeams.size();
    std::vector<uint32_t> slots(nBeams, 0);
    std::vector<double>   exact(nBeams, 0.0);

    if (inWarmup)
    {
        if (!s_warmupSlots.empty())
        {
            // Use pre-configured warmup slots (e.g. 3/5/7 for asym)
            for (uint32_t k = 0; k < nBeams; ++k)
            {
                auto it = s_warmupSlots.find(m_enabledBeams[k]);
                slots[k] = (it != s_warmupSlots.end()) ? it->second : 1;
                exact[k] = static_cast<double>(slots[k]);
            }
        }
        else
        {
            // No warmup config: distribute equally
            uint32_t base = m_planSuperframes / nBeams;
            uint32_t rem  = m_planSuperframes % nBeams;
            for (uint32_t k = 0; k < nBeams; ++k)
            {
                slots[k] = base + (k < rem ? 1 : 0);
                exact[k] = static_cast<double>(slots[k]);
            }
        }
    }
    else if (totalWeight > 0.0)
    {
        for (uint32_t k = 0; k < nBeams; ++k)
        {
            exact[k] = weights[m_enabledBeams[k]] / totalWeight * m_planSuperframes;
            slots[k] = static_cast<uint32_t>(exact[k]);   // floor
        }
    }
    else
    {
        // All weights zero post-warmup: distribute equally
        uint32_t base = m_planSuperframes / nBeams;
        for (uint32_t k = 0; k < nBeams; ++k)
        {
            slots[k] = base;
            exact[k] = static_cast<double>(base);
        }
    }

    // Distribute remaining slots by largest fractional remainder
    uint32_t allocated = std::accumulate(slots.begin(), slots.end(), 0u);
    uint32_t remaining = m_planSuperframes - allocated;

    std::vector<std::pair<double, uint32_t>> remainders;
    for (uint32_t k = 0; k < nBeams; ++k)
    {
        remainders.push_back({exact[k] - slots[k], k});
    }
    std::sort(remainders.begin(), remainders.end(), std::greater<>());
    for (uint32_t r = 0; r < remaining; ++r)
    {
        slots[remainders[r].second]++;
    }

    // Ensure every enabled beam gets at least 1 slot when there are enough slots
    if (m_planSuperframes >= nBeams)
    {
        for (uint32_t k = 0; k < nBeams; ++k)
        {
            if (slots[k] == 0)
            {
                // Give this beam 1 slot from the beam with the most slots
                uint32_t maxK = std::max_element(slots.begin(), slots.end()) - slots.begin();
                if (slots[maxK] > 1)
                {
                    slots[maxK]--;
                    slots[k] = 1;
                }
            }
        }
    }

    // ── Step 5: interleave slots so beams alternate within the plan ───────
    // Bresenham accumulator: each beam accumulates credit equal to its slot
    // count every step; the beam with the highest credit wins the slot and
    // pays back m_planSuperframes. This minimises the maximum gap between
    // consecutive services for every beam, regardless of how many beams
    // there are or how unequal their allocations are.
    m_plan.clear();
    m_plan.reserve(m_planSuperframes);
    std::vector<int32_t> credit(nBeams, 0);
    std::vector<uint32_t> remaining_slots = slots;

    for (uint32_t pos = 0; pos < m_planSuperframes; ++pos)
    {
        // Accumulate credit for beams that still have slots left.
        for (uint32_t k = 0; k < nBeams; ++k)
            if (remaining_slots[k] > 0)
                credit[k] += static_cast<int32_t>(slots[k]);

        // Pick the beam with the highest credit that still has slots.
        int32_t best = -1;
        for (uint32_t k = 0; k < nBeams; ++k)
        {
            if (remaining_slots[k] == 0)
                continue;
            if (best < 0 || credit[k] > credit[best])
                best = static_cast<int32_t>(k);
        }

        // All slots placed before filling m_planSuperframes positions (idle time).
        if (best < 0)
            break;

        m_plan.push_back(m_enabledBeams[best]);
        remaining_slots[best]--;
        credit[best] -= static_cast<int32_t>(m_planSuperframes);
    }

    // Build a beamId→slots map for logging
    std::map<uint32_t, uint32_t> beamSlots;
    for (uint32_t k = 0; k < nBeams; ++k)
        beamSlots[m_enabledBeams[k]] = slots[k];

    // ── CSV log: one row per beam per plan period ─────────────────────────
    // prev_forecast = forecast made last period predicting X[n] (current actual).
    // Must be read BEFORE m_prevForecast is overwritten with this period's forecasts.
    int64_t tMs = Simulator::Now().GetMilliSeconds();
    for (uint32_t bid : m_enabledBeams)
    {
        double prevFc = (prevWasArfima && m_prevForecast.count(bid))
                        ? m_prevForecast[bid] : -1.0; // -1 flags warmup/unavailable
        m_planLog << tMs << ","
                  << bid << ","
                  << gwQueue[bid] << ","
                  << (m_satArrivalBytes.count(bid) ? m_satArrivalBytes.at(bid) : 0) << ","
                  << std::fixed << std::setprecision(2) << forecasts[bid] << ","
                  << weights[bid] << ","
                  << beamSlots[bid] << ","
                  << std::setprecision(2) << prevFc << "\n";
    }

    // ── Store forecast for next period's error calculation ────────────────
    if (!inWarmup)
        m_prevForecast = forecasts;
    // Print a progress line every second of simulation time
    int64_t tSec = tMs / 1000;
    if (tSec != m_lastPrintedSec && tMs % 1000 < static_cast<int64_t>(m_planSuperframes) + 1)
    {
        m_lastPrintedSec = tSec;
        std::cout << "[" << tSec << "s]  slots:";
        for (uint32_t k = 0; k < nBeams; ++k)
            std::cout << "  b" << m_enabledBeams[k] << "=" << slots[k];
        std::cout << std::endl;
    }
}

std::vector<uint32_t>
SatDynamicBstp::GetNextConf()
{
    // Arrival bytes accumulate continuously in SatOrbiterUserLlc::m_arrivalBytes
    // and are read+reset atomically in RegeneratePlan() via ReadAndResetArrivalBytes().
    // No per-slot sampling needed here.

    // Regenerate at the start of each plan horizon
    if (m_planIndex == 0 || m_plan.empty())
    {
        RegeneratePlan();
    }

    uint32_t activeBeam = m_plan[m_planIndex];
    m_planIndex = (m_planIndex + 1) % m_planSuperframes;

    std::vector<uint32_t> nextConf;
    nextConf.push_back(1); // validity: 1 superframe (1 ms dwell)
    nextConf.push_back(activeBeam);

    return nextConf;
}

} // namespace ns3
