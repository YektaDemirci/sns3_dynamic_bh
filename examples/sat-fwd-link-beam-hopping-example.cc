/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 Magister Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Jani Puttonen <jani.puttonen@magister.fi>
 *
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"

#include <fstream>
#include "ns3/satellite-dynamic-bstp.h"

using namespace ns3;

/**
 * @file sat-fwd-link-beam-hopping-example.cc
 * @ingroup satellite
 *
 * This simulation script is an example of FWD link beam hopping
 * configuration.
 */

NS_LOG_COMPONENT_DEFINE("sat-fwd-link-beam-hopping-example");

/**
 * @brief Global variables to track previous queue sizes
 */
static uint32_t g_prevGwQueueBytes = 0;
static uint32_t g_prevGwQueuePackets = 0;
static uint32_t g_prevSatQueueBytes = 0;
static uint32_t g_prevSatQueuePackets = 0;

/**
 * @brief Global variables to track packet generation
 */
static uint32_t g_packetsGeneratedThisPeriod = 0;
static uint32_t g_totalPacketsGenerated = 0;
static uint64_t g_bytesGeneratedThisPeriod = 0;
static uint64_t g_totalBytesGenerated = 0;
static std::ofstream g_packetGenFile;

/**
 * @brief Callback to track packet transmission from OnOff applications
 * @param packet Transmitted packet
 */
void
PacketTxCallback(Ptr<const Packet> packet)
{
    g_packetsGeneratedThisPeriod++;
    g_totalPacketsGenerated++;
    g_bytesGeneratedThisPeriod += packet->GetSize();
    g_totalBytesGenerated += packet->GetSize();
}

/**
 * @brief Log packet generation rate every Xms to CSV file
 */
void
LogPacketGenerationRate()
{
    if (g_packetGenFile.is_open())
    {
        g_packetGenFile << Simulator::Now().GetSeconds() << ","
                        << g_packetsGeneratedThisPeriod << ","
                        << g_totalPacketsGenerated << ","
                        << g_bytesGeneratedThisPeriod << ","
                        << g_totalBytesGenerated << ","
                        << (g_bytesGeneratedThisPeriod * 8) << ","
                        << (g_totalBytesGenerated * 8) << std::endl;
    }
    
    // Reset counters for next period
    g_packetsGeneratedThisPeriod = 0;
    g_bytesGeneratedThisPeriod = 0;
    
    // Schedule next log
    Simulator::Schedule(MilliSeconds(10), &LogPacketGenerationRate);
}


/**
 * @brief Function to log queue sizes at GW, satellite, and UTs
 */
void
LogQueueSizes()
{
    // Access the topology singleton
    Ptr<SatTopology> topology = Singleton<SatTopology>::Get();

    uint32_t totalGwQueueBytes = 0;
    uint32_t totalGwQueuePackets = 0;
    uint32_t totalSatQueueBytes = 0;
    uint32_t totalSatQueuePackets = 0;

    // Log GW queue sizes (forward link)
    for (uint32_t gwId = 0; gwId < topology->GetNGwNodes(); ++gwId)
    {
        Ptr<Node> gwNode = topology->GetGwNode(gwId);
        for (uint32_t i = 0; i < gwNode->GetNDevices(); ++i)
        {
            Ptr<NetDevice> device = gwNode->GetDevice(i);
            Ptr<SatNetDevice> satNetDevice = DynamicCast<SatNetDevice>(device);
            if (satNetDevice)
            {
                Ptr<SatLlc> llc = satNetDevice->GetLlc();
                if (llc)
                {
                    uint32_t bytes = llc->GetNBytesInQueue();
                    uint32_t packets = llc->GetNPacketsInQueue();
                    totalGwQueueBytes += bytes;
                    totalGwQueuePackets += packets;
                }
            }
        }
    }

    // Log Satellite (Orbiter) queue sizes
    for (uint32_t satId = 0; satId < topology->GetNOrbiterNodes(); ++satId)
    {
        Ptr<Node> satNode = topology->GetOrbiterNode(satId);
        Ptr<SatOrbiterNetDevice> orbiterNetDevice = topology->GetOrbiterNetDevice(satNode);
        
        if (orbiterNetDevice)
        {
            // Get all user MACs to find which beams exist
            std::map<uint32_t, Ptr<SatMac>> userMacs = orbiterNetDevice->GetUserMac();
            
            // For each beam, get the user LLC (forward link transmission)
            for (auto macIt = userMacs.begin(); macIt != userMacs.end(); ++macIt)
            {
                uint32_t beamId = macIt->first;
                Ptr<SatOrbiterUserLlc> userLlc = topology->GetOrbiterUserLlc(satNode, beamId);
                if (userLlc)
                {
                    // Cast to base SatLlc to use GetNBytesInQueue()
                    Ptr<SatLlc> llc = DynamicCast<SatLlc>(userLlc);
                    if (llc)
                    {
                        uint32_t bytes = llc->GetNBytesInQueue();
                        uint32_t packets = llc->GetNPacketsInQueue();
                        totalSatQueueBytes += bytes;
                        totalSatQueuePackets += packets;
                    }
                }
            }
        }
    }

    // Calculate differences
    int32_t gwBytesDiff = totalGwQueueBytes - g_prevGwQueueBytes;
    int32_t gwPacketsDiff = totalGwQueuePackets - g_prevGwQueuePackets;
    int32_t satBytesDiff = totalSatQueueBytes - g_prevSatQueueBytes;
    int32_t satPacketsDiff = totalSatQueuePackets - g_prevSatQueuePackets;

    // Print summary with differences
    std::cout << Simulator::Now().GetSeconds() << "s: "
              << "GW Queue: " << totalGwQueueBytes << " bytes (" << totalGwQueuePackets << " packets)"
              << " [Δ " << (gwBytesDiff >= 0 ? "+" : "") << gwBytesDiff << " bytes, "
              << (gwPacketsDiff >= 0 ? "+" : "") << gwPacketsDiff << " packets], "
              << "Sat Queue: " << totalSatQueueBytes << " bytes (" << totalSatQueuePackets << " packets)"
              << " [Δ " << (satBytesDiff >= 0 ? "+" : "") << satBytesDiff << " bytes, "
              << (satPacketsDiff >= 0 ? "+" : "") << satPacketsDiff << " packets]"
              << std::endl;

    // Update previous values
    g_prevGwQueueBytes = totalGwQueueBytes;
    g_prevGwQueuePackets = totalGwQueuePackets;
    g_prevSatQueueBytes = totalSatQueueBytes;
    g_prevSatQueuePackets = totalSatQueuePackets;

    // Schedule next log
    Simulator::Schedule(Seconds(1.0), &LogQueueSizes);
}

int
main(int argc, char* argv[])
{
    uint32_t endUsersPerUt(1);
    Time simLength(Seconds(5.0));
    std::string scenario("leo-tlst3-beam-hopping");
    std::string scheduler("arfima"); // "arfima" or "fixed"
    std::string users("sym");        // "sym" (150/150/150) or "asym" (90/150/210)
    uint32_t planSuperframes(15);    // plan horizon in superframes (1 ms each)
    double shape55(1.04);            // Pareto shape for beam 55
    double shape56(1.04);            // Pareto shape for beam 56
    double shape57(1.04);            // Pareto shape for beam 57
    double hurst55(0.88);            // Hurst parameter for beam 55 (fgn scheduler)
    double hurst56(0.88);            // Hurst parameter for beam 56 (fgn scheduler)
    double hurst57(0.88);            // Hurst parameter for beam 57 (fgn scheduler)
    std::string expId("");           // optional experiment label, e.g. "Exp1"
    double userBw(300e6);            // FwdUserCarrierAllocatedBandwidth in Hz

    // Parse arguments first (before creating SimulationHelper which needs the name)
    CommandLine cmd;
    cmd.AddValue("simTime",          "Length of simulation",                               simLength);
    cmd.AddValue("scenario",         "Scenario name",                                      scenario);
    cmd.AddValue("scheduler",        "'arfima' (dynamic) or 'fixed' (static)",             scheduler);
    cmd.AddValue("users",            "'sym' (150/150/150) or 'asym' (90/150/210)",         users);
    cmd.AddValue("planSuperframes",  "Plan horizon in superframes (1 ms each)",            planSuperframes);
    cmd.AddValue("shape55",          "Pareto ON/OFF shape for beam 55",                    shape55);
    cmd.AddValue("shape56",          "Pareto ON/OFF shape for beam 56",                    shape56);
    cmd.AddValue("shape57",          "Pareto ON/OFF shape for beam 57",                    shape57);
    cmd.AddValue("hurst55",          "Hurst parameter for beam 55 (fgn scheduler)",        hurst55);
    cmd.AddValue("hurst56",          "Hurst parameter for beam 56 (fgn scheduler)",        hurst56);
    cmd.AddValue("hurst57",          "Hurst parameter for beam 57 (fgn scheduler)",        hurst57);
    cmd.AddValue("expId",            "Experiment label prefix (e.g. Exp1), omit to skip", expId);
    cmd.AddValue("userBw",           "FwdUserCarrierAllocatedBandwidth in Hz (e.g. 300e6)", userBw);
    cmd.Parse(argc, argv);

    // Apply plan horizon before any SatDynamicBstp is constructed
    SatDynamicBstp::SetPlanSuperframes(planSuperframes);

    time_t t = time(nullptr);
    struct tm* now = localtime(&t);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "%d%B%H-%M", now);
    bool mixedHurst = (shape55 != shape56 || shape56 != shape57);
    uint32_t bwMHz = static_cast<uint32_t>(userBw / 1e6);
    std::string simulationName = (expId.empty() ? "" : expId + "_")
        + std::string(buffer)
        + "_p" + std::to_string(planSuperframes)
        + "_" + scheduler
        + "_" + users
        + (mixedHurst ? "_mixHurst" : "_fxdHurst")
        + "_bw" + std::to_string(bwMHz) + "MHz"
        + "_run" + std::to_string(RngSeedManager::GetRun());

    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>(simulationName);
    simulationHelper->AddDefaultUiArguments(cmd);  // registers --OutputPath etc.
    simulationHelper->SetDefaultValues();

    bool isLeo = (scenario.find("leo") != std::string::npos);

    if (isLeo)
    {
        Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                           EnumValue(SatEnums::REGENERATION_NETWORK));
        Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                           EnumValue(SatEnums::REGENERATION_NETWORK));
        Config::SetDefault("ns3::SatGwMac::SendNcrBroadcast", BooleanValue(false));
        Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest", BooleanValue(true));
        Config::SetDefault("ns3::SatAntennaGainPattern::EarthFixedBeams", BooleanValue(true));
        Config::SetDefault("ns3::SatAntennaGainPattern::MinAcceptableAntennaGainDb", DoubleValue(44.0));
    }

    // 2000 packets each 512 bytes for 500 users 2000x500x512=512MB
    Config::SetDefault("ns3::SatQueue::MaxPackets", UintegerValue(2000));

    Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(false));

    simulationHelper->SetUserCountPerUt(endUsersPerUt);

    // Set user link carrier bandwidth before ConfigureFwdLinkBeamHopping() and before
    // CreateSatScenario() (which calls ConfigureFwdLinkBeamHopping() internally again).
    // ConfigureFwdLinkBeamHopping() no longer sets this attribute, so this value persists.
    Config::SetDefault("ns3::SatConf::FwdUserCarrierAllocatedBandwidth", DoubleValue(userBw));

    simulationHelper->ConfigureFwdLinkBeamHopping();
    simulationHelper->SetSimulationTime(simLength.GetSeconds());

    // LEO: 3 adjacent beams around Montreal (equilateral triangle, ~200km spacing).
    //   Beam 57: 45.5N -73.6W (Montreal)
    //   Beam 56: 45.5N -76.2W (~200km west, Kingston area)
    //   Beam 55: 47.1N -74.9W (~200km north, Laurentians)
    if (isLeo)
    {
        // Set beam-hopping beams and per-beam UT counts.
        simulationHelper->SetBeams("55 56 57");
        if (users == "asym")
        {
            // Asymmetric: 90/150/210 UTs in beams 55/56/57 (ratio 3:5:7).
            simulationHelper->SetUtCountPerBeam(55, 90);
            simulationHelper->SetUtCountPerBeam(56, 150);
            simulationHelper->SetUtCountPerBeam(57, 210);
            std::cout << "UT distribution: asym (90/150/210 per beam 55/56/57)" << std::endl;
        }
        else
        {
            simulationHelper->SetUtCountPerBeam(150);
            std::cout << "UT distribution: sym (150/150/150 per beam)" << std::endl;
        }
    }
    else
    {
        simulationHelper->SetBeams("5");
        simulationHelper->SetUtCountPerBeam(500);
    }

    simulationHelper->LoadScenario(scenario);

    // ── Scheduler configuration ───────────────────────────────────────────
    // Must be set BEFORE CreateSatScenario() because Init() fires RegeneratePlan()
    // synchronously at the end of CreateSatScenario().
    // Proportional slot counts used for fixed mode and as ARFIMA warmup, must sum to 15.
    // Slot counts must sum to planSuperframes.
    // sym:  equal split → planSuperframes/3 each (3:5:7 ratio not needed)
    // asym: 3:5:7 ratio → planSuperframes * 3/15, 5/15, 7/15
    std::map<uint32_t, uint32_t> proportionalSlots;
    if (users == "asym")
    {
        proportionalSlots[55] = planSuperframes * 3 / 15;
        proportionalSlots[56] = planSuperframes * 5 / 15;
        proportionalSlots[57] = planSuperframes - proportionalSlots[55] - proportionalSlots[56];
    }
    else
    {
        proportionalSlots[55] = planSuperframes / 3;
        proportionalSlots[56] = planSuperframes / 3;
        proportionalSlots[57] = planSuperframes - proportionalSlots[55] - proportionalSlots[56];
    }

    if (isLeo && scheduler == "fixed")
    {
        SatDynamicBstp::SetFixedSlots(proportionalSlots);
        std::cout << "Scheduler: fixed  slots 55=" << proportionalSlots[55]
                  << " 56=" << proportionalSlots[56]
                  << " 57=" << proportionalSlots[57] << std::endl;
    }
    else
    {
        SatDynamicBstp::SetWarmupSlots(proportionalSlots);
        if (scheduler == "fgn")
        {
            std::map<uint32_t, double> hurstParams = {{55, hurst55}, {56, hurst56}, {57, hurst57}};
            SatDynamicBstp::SetHurstParams(hurstParams);
            std::cout << "Scheduler: fgn (dynamic)  H55=" << hurst55
                      << " H56=" << hurst56 << " H57=" << hurst57
                      << "  warmup slots 55=" << proportionalSlots[55]
                      << " 56=" << proportionalSlots[56]
                      << " 57=" << proportionalSlots[57] << std::endl;
        }
        else if (scheduler == "arfima")
        {
            std::cout << "Scheduler: arfima (dynamic)  warmup slots 55=" << proportionalSlots[55]
                      << " 56=" << proportionalSlots[56]
                      << " 57=" << proportionalSlots[57] << std::endl;
        }
        else {
            throw std::runtime_error("Scheduler '" + scheduler + "' is not implemented.");
        }
    }

    // Create the scenario — triggers SatBstpController::Initialize() which calls
    // RegeneratePlan() synchronously, so slot config must be set above first.
    simulationHelper->CreateSatScenario();

    // 512*1000*8 bits every millisecond = 4 Mbps per user
    // Install traffic model; CBR stands for constant bit rate
    // simulationHelper->GetTrafficHelper()->AddCbrTraffic(
    //     SatTrafficHelper::FWD_LINK,
    //     SatTrafficHelper::UDP,
    //     MilliSeconds(1),
    //     512,
    //     NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)), // Only one of the gateways
    //     Singleton<SatTopology>::Get()->GetUtUserNodes(),
    //     MilliSeconds(1),
    //     simLength,
    //     MilliSeconds(1));

    // Install per-beam traffic with different Pareto shapes.
    // Shape closer to 1.0 = heavier tail = longer continuous ON and OFF periods.
    // All beams: 1 Mbps, 150 UTs — only burstiness differs.
    //   beam 55: shape 1.04 (very bursty, heavy tail)
    //   beam 56: shape 1.24 (medium)
    //   beam 57: shape 1.44 (least bursty, lightest tail)
    {
        Ptr<SatTopology> topo = Singleton<SatTopology>::Get();
        NodeContainer gwSrc(topo->GetGwUserNode(0));
        NodeContainer utNodes = topo->GetUtNodes();
        NodeContainer utUsers55, utUsers56, utUsers57;
        for (uint32_t u = 0; u < utNodes.GetN(); ++u)
        {
            Ptr<Node> ut = utNodes.Get(u);
            uint32_t bid = topo->GetUtBeamId(ut);
            NodeContainer utUserNodes = topo->GetUtUserNodes(ut);
            for (uint32_t i = 0; i < utUserNodes.GetN(); ++i)
            {
                if      (bid == 55) utUsers55.Add(utUserNodes.Get(i));
                else if (bid == 56) utUsers56.Add(utUserNodes.Get(i));
                else if (bid == 57) utUsers57.Add(utUserNodes.Get(i));
            }
        }
        std::cout << "Per-beam traffic split: b55=" << utUsers55.GetN()
                  << " b56=" << utUsers56.GetN()
                  << " b57=" << utUsers57.GetN() << std::endl;

        struct { NodeContainer* dsts; double shape; } beamTraffic[] = {
            { &utUsers55, shape55 },
            { &utUsers56, shape56 },
            { &utUsers57, shape57 },
        };
        for (auto& bt : beamTraffic)
        {
            if (bt.dsts->GetN() == 0) continue;
            std::string rv = std::string("ns3::ParetoRandomVariable[Scale=0.01|Shape=") + std::to_string(bt.shape) + "|Bound=0]";
            simulationHelper->GetTrafficHelper()->AddOnOffTraffic(
                SatTrafficHelper::FWD_LINK,
                SatTrafficHelper::UDP,
                DataRate("1Mbps"),
                512,
                gwSrc,
                *bt.dsts,
                rv, rv,
                MilliSeconds(1),
                simLength,
                MilliSeconds(0));
        }
    }

    auto stats = simulationHelper->GetStatisticsContainer();
    
    stats->AddGlobalFwdAppThroughput(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdAppThroughput(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    stats->AddPerBeamFwdUserMacThroughput(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdUserPhyThroughput(SatStatsHelper::OUTPUT_SCALAR_FILE);
    
    stats->AddPerBeamBeamServiceTime(SatStatsHelper::OUTPUT_SCALAR_FILE);

    stats->AddGlobalFwdAppDelay(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddGlobalFwdAppDelay(SatStatsHelper::OUTPUT_CDF_FILE);

    stats->AddPerBeamFwdAppDelay(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdAppDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

    stats->AddPerBeamFwdDevDelay(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdPhyDelay(SatStatsHelper::OUTPUT_SCALAR_FILE);
    stats->AddPerBeamFwdMacDelay(SatStatsHelper::OUTPUT_SCALAR_FILE);

    // Add jitter statistics
    stats->AddPerBeamFwdDevJitter(SatStatsHelper::OUTPUT_SCALAR_FILE);

    simulationHelper->EnableProgressLogs();

    // Open CSV file for packet generation logging
    g_packetGenFile.open("packet_generation.csv");
    if (g_packetGenFile.is_open())
    {
        g_packetGenFile << "Time_s,Packets_Last_Xms,Total_Packets,Bytes_Last_Xms,Total_Bytes,Bits_Last_Xms,Total_Bits" << std::endl;
        std::cout << "Created packet_generation.csv for logging packet generation" << std::endl;
    }
    else
    {
        std::cerr << "ERROR: Failed to open packet_generation.csv" << std::endl;
    }

    // Connect packet transmission traces for OnOff applications
    Ptr<Node> gwUserNode = Singleton<SatTopology>::Get()->GetGwUserNode(0);
    uint32_t connectedApps = 0;
    for (uint32_t i = 0; i < gwUserNode->GetNApplications(); ++i)
    {
        Ptr<Application> app = gwUserNode->GetApplication(i);
        Ptr<OnOffApplication> onOffApp = DynamicCast<OnOffApplication>(app);
        if (onOffApp)
        {
            onOffApp->TraceConnectWithoutContext("Tx", MakeCallback(&PacketTxCallback));
            connectedApps++;
        }
    }
    std::cout << "Connected packet Tx trace for " << connectedApps << " OnOff applications" << std::endl;
    
    if (connectedApps == 0)
    {
        std::cerr << "WARNING: No OnOff applications found to trace!" << std::endl;
    }

    // Schedule packet generation logging to start after 1 second and repeat every Xms
    Simulator::Schedule(Seconds(1.0), &LogPacketGenerationRate);

    // Schedule queue size logging to start after 1 second and repeat every 100ms
    Simulator::Schedule(Seconds(1.0), &LogQueueSizes);

    std::cout << "UT count: " 
          << Singleton<SatTopology>::Get()->GetNUtNodes() 
          << std::endl;

    simulationHelper->RunSimulation();

    // Close the packet generation CSV file
    if (g_packetGenFile.is_open())
    {
        g_packetGenFile.close();
        std::cout << "Closed packet_generation.csv" << std::endl;
    }

    return 0;
}
