#ifndef SATELLITE_DYNAMIC_BSTP_H
#define SATELLITE_DYNAMIC_BSTP_H

#include "ns3/object.h"
#include <deque>
#include <fstream>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

namespace ns3
{

/**
 * Dynamic beam-hopping plan scheduler.
 *
 * Every m_planSuperframes superframes (default 10 → 10 ms at 1 ms/superframe)
 * a new plan is generated and executed one slot at a time (1 beam active per
 * 1 ms slot).
 *
 * Slot allocation is proportional to per-beam weights:
 *
 *   w_i = queue_depth_i + gamma * arfima_forecast_of_arrivals_i
 *
 * where:
 *   queue_depth_i     = bytes currently queued in beam i's orbiter user LLC
 *                       at plan boundary — captures unmet backlog.
 *   arfima_forecast_i = ARFIMA(2,0,0) 1-step-ahead forecast of per-period
 *                       arrival bytes, trained on the last 24 arrival samples
 *                       (each sample = bytes arriving in one 15 ms period).
 *   gamma             = weight on forecast term (default 1.0).
 *
 * During the first m_warmupPlans plan periods (default 20 → 300 ms) the
 * forecast term is suppressed (gamma_eff = 0) and slots are distributed
 * equally to prevent cold-start starvation.
 */
class SatDynamicBstp : public Object
{
  public:
    static TypeId GetTypeId(void);
    SatDynamicBstp();
    virtual ~SatDynamicBstp();

    /**
     * Returns the next BSTP configuration vector.
     * First element: validity in superframes (always 1).
     * Second element: the single beam ID to activate for this superframe.
     */
    std::vector<uint32_t> GetNextConf();

    void AddEnabledBeamInfo(uint32_t beamId,
                            uint32_t userFreqId,
                            uint32_t feederFreqId,
                            uint32_t gwId);

    /**
     * Switch to fixed (static) scheduling: each beam gets a pre-set number of
     * slots every plan period regardless of queue depth or forecast.
     * This is a static call — any SatDynamicBstp instance created afterwards
     * (or already existing) will pick up this configuration.
     * beamSlots values must sum to m_planSuperframes (default 15).
     */
    static void SetFixedSlots(const std::map<uint32_t, uint32_t>& beamSlots);

    /**
     * Set per-beam slot counts to use during the warmup period.
     * After warmup the scheduler switches to ARFIMA weights.
     * beamSlots values must sum to m_planSuperframes.
     */
    static void SetWarmupSlots(const std::map<uint32_t, uint32_t>& beamSlots);

    /**
     * Override the plan horizon (number of superframes per plan period).
     * Must be called before any SatDynamicBstp instance is constructed.
     */
    static void SetPlanSuperframes(uint32_t n);

    /**
     * Set per-beam Hurst parameters for the FGN forecast daemon (fgn.py).
     * Must be called before the first SatDynamicBstp instance is constructed.
     */
    static void SetHurstParams(const std::map<uint32_t, double>& hurstParams);

  private:
    void RegeneratePlan();

    /** Read and reset per-beam arrival byte counters from orbiter user LLCs. */
    std::map<uint32_t, uint64_t> ReadAndResetArrivalBytes();

    std::vector<uint32_t> m_enabledBeams;

    // gwId and satId stored per beam for GW LLC lookups
    std::map<uint32_t, uint32_t> m_beamGwId;   // beamId → gwId
    std::map<uint32_t, uint32_t> m_beamSatId;  // beamId → satId (first sat found)

    // UT count per beam — used to weight demand when per-user rates are equal.
    // Populated lazily on first call to RegeneratePlan().
    std::map<uint32_t, uint32_t> m_beamUtCount;

    // Number of 1 ms superframes per plan horizon (default 10 → 10 ms plan)
    uint32_t m_planSuperframes;

    // Number of past GW-queue snapshots kept per beam for the ARFIMA forecast
    uint32_t m_forecastWindow;

    // Arrival byte history per beam: each entry = bytes enqueued during one plan period
    std::map<uint32_t, std::deque<double>> m_gwQueueHistory;

    // Pre-computed plan: ordered sequence of beam IDs (one entry per superframe)
    std::vector<uint32_t> m_plan;

    // Index into m_plan for the current superframe slot
    uint32_t m_planIndex;

    // Number of plan periods elapsed; used to skip ARFIMA during warmup
    uint32_t m_planCount;

    // Number of plan periods to run on pure GW-queue weight before enabling forecast
    // Default 30 → 300 ms at 10 ms/plan
    uint32_t m_warmupPlans;

    // Dimensionless weight on the ARFIMA forecast term (default 1.0)
    // w_i = queue_depth_i + gamma * forecast_i
    double m_gamma;

    // Fixed-scheduling mode: static map shared across all instances.
    // If non-empty, slots are taken directly from here every plan period.
    static std::map<uint32_t, uint32_t> s_fixedSlots;
    static std::map<uint32_t, uint32_t> s_warmupSlots;
    static uint32_t s_planSuperframes;
    static std::map<uint32_t, double>   s_hurstParams;

    // CSV log: one row per plan period per beam
    std::ofstream m_planLog;

    // Per-beam demand logs: beamXX.csv with columns time_ms, demand_bytes
    std::map<uint32_t, std::ofstream> m_beamDemandLogs;

    // SAT LLC arrival bytes read in the last plan period (for CSV logging)
    std::map<uint32_t, uint64_t> m_satArrivalBytes;

    // Last simulation second at which a progress line was printed
    int64_t m_lastPrintedSec;

    // ── Forecast accuracy tracking ─────────────────────────────────────────
    // forecast made at plan T is compared to actual arrivals at plan T+1
    std::map<uint32_t, double> m_prevForecast;   // last period's forecast per beam

    // ── ARFIMA subprocess (far.py) ─────────────────────────────────────────
    pid_t m_farPid;
    int   m_farWriteFd;
    int   m_farReadFd;

    void StartFarDaemon();
    double QueryFar(uint32_t beamId, const std::deque<double>& samples);
};

} // namespace ns3

#endif
