/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Magister Solutions
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
 * Author: Bastien Tauran <bastien.tauran@viveris.fr>
 *
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"

using namespace ns3;

/**
 * \file sat-regeneration-example.cc
 * \ingroup satellite
 *
 * \brief This file gives an example of satellite regeneration.
 *        It allows to launch a simulation with FWD and RTN CBR traffics,
 *        and different levels of regeneration.
 *         - On FWD link: transparent, physical and network
 *         - On RTN link: transparent, physical, link and network
 *        Several statistics are generated.
 */

NS_LOG_COMPONENT_DEFINE("sat-regeneration-example");

/**
 * @brief Function to log queue sizes at GW and satellite
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

    // Print summary
    std::cout << Simulator::Now().GetSeconds() << "s: "
              << "GW: " << totalGwQueueBytes << " bytes (" << totalGwQueuePackets << " packets), "
              << "Satellite: " << totalSatQueueBytes << " bytes (" << totalSatQueuePackets 
              << " packets)" << std::endl;

    // Schedule next log
    Simulator::Schedule(Seconds(1.0), &LogQueueSizes);
}

int
main(int argc, char* argv[])
{
    uint32_t beamIdInFullScenario = 10;
    uint32_t packetSize = 50000;
    std::string interval = "1ms";
    std::string scenario = "simple";
    std::string forwardRegeneration = "regeneration_network";
    std::string returnRegeneration = "regeneration_network";

    std::map<std::string, SatHelper::PreDefinedScenario_t> mapScenario{
        {"simple", SatHelper::SIMPLE},
        {"larger", SatHelper::LARGER},
        {"full", SatHelper::FULL}};
    std::map<std::string, SatEnums::RegenerationMode_t> mapForwardRegeneration{
        {"transparent", SatEnums::TRANSPARENT},
        {"regeneration_phy", SatEnums::REGENERATION_PHY},
        {"regeneration_network", SatEnums::REGENERATION_NETWORK}};
    std::map<std::string, SatEnums::RegenerationMode_t> mapReturnRegeneration{
        {"transparent", SatEnums::TRANSPARENT},
        {"regeneration_phy", SatEnums::REGENERATION_PHY},
        {"regeneration_link", SatEnums::REGENERATION_LINK},
        {"regeneration_network", SatEnums::REGENERATION_NETWORK}};

    Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("example-regeneration");

    // read command line parameters given by user
    CommandLine cmd;
    cmd.AddValue("beamIdInFullScenario",
                 "Id where Sending/Receiving UT is selected in FULL scenario. (used only when "
                 "scenario is full) ",
                 beamIdInFullScenario);
    cmd.AddValue("packetSize", "Size of constant packet (bytes)", packetSize);
    cmd.AddValue("interval", "Interval to sent packets in seconds, (e.g. (1s))", interval);
    cmd.AddValue("scenario", "Test scenario to use. (simple, larger or full)", scenario);
    cmd.AddValue(
        "forwardRegeneration",
        "Regeneration mode on forward link (transparent, regeneration_phy or regeneration_network)",
        forwardRegeneration);
    cmd.AddValue("returnRegeneration",
                 "Regeneration mode on forward link (transparent, regeneration_phy, "
                 "regeneration_link or regeneration_network)",
                 returnRegeneration);
    simulationHelper->AddDefaultUiArguments(cmd);
    cmd.Parse(argc, argv);

    SatHelper::PreDefinedScenario_t satScenario = mapScenario[scenario];
    SatEnums::RegenerationMode_t forwardLinkRegenerationMode =
        mapForwardRegeneration[forwardRegeneration];
    SatEnums::RegenerationMode_t returnLinkRegenerationMode =
        mapReturnRegeneration[returnRegeneration];

    /// Set regeneration mode
    Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                       EnumValue(forwardLinkRegenerationMode));
    Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                       EnumValue(returnLinkRegenerationMode));
    Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
    Config::SetDefault("ns3::SatOrbiterUserPhy::QueueSize", UintegerValue(100000));
    
    // Increase SatQueue max packets (this is the actual queue limit)
    Config::SetDefault("ns3::SatQueue::MaxPackets", UintegerValue(100000));

    /// Enable ACM
    Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(false)); //Demirci changed!

    /// Set simulation output details
    Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));

    /// Enable packet trace
    Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));

    // Set tag, if output path is not explicitly defined
    simulationHelper->SetOutputTag(scenario);

    simulationHelper->SetSimulationTime(Seconds(30));

    // Set beam ID
    std::stringstream beamsEnabled;
    beamsEnabled << beamIdInFullScenario;
    simulationHelper->SetBeams(beamsEnabled.str());

    LogComponentEnable("sat-regeneration-example", LOG_LEVEL_INFO);

    Config::SetDefault("ns3::SatConf::FwdCarrierAllocatedBandwidth", DoubleValue(50e+06)); // 50 MHz

    simulationHelper->LoadScenario("geo-33E");

    simulationHelper->CreateSatScenario(satScenario);

    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::FWD_LINK,
        SatTrafficHelper::UDP,
        Time(interval),
        packetSize,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0),
        Seconds(29.0),
        Seconds(0));

    simulationHelper->GetTrafficHelper()->AddCbrTraffic(
        SatTrafficHelper::RTN_LINK,
        SatTrafficHelper::UDP,
        Time(interval),
        packetSize,
        NodeContainer(Singleton<SatTopology>::Get()->GetGwUserNode(0)),
        Singleton<SatTopology>::Get()->GetUtUserNodes(),
        Seconds(1.0),
        Seconds(29.0),
        Seconds(0));

    NS_LOG_INFO("--- sat-regeneration-example ---");
    NS_LOG_INFO("  Scenario used: " << scenario);
    if (scenario == "full")
    {
        NS_LOG_INFO("  UT used in full scenario from beam: " << beamIdInFullScenario);
    }
    NS_LOG_INFO("  PacketSize: " << packetSize);
    NS_LOG_INFO("  Interval: " << interval);
    NS_LOG_INFO("  ");

    // To store attributes to file
    Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.xml"));
    Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
    Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
    ConfigStore outputConfig;
    outputConfig.ConfigureDefaults();

    Ptr<SatStatsHelperContainer> s = simulationHelper->GetStatisticsContainer();

    // Throughput statistics
    s->AddPerUtFwdFeederPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddGlobalFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddGlobalRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // Delay statistics
    s->AddPerUtFwdPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // link delay statistics
    s->AddPerUtFwdFeederPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // Jitter statistics
    s->AddPerUtFwdPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // Link jitter statistics
    s->AddPerUtFwdFeederPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // Phy RX statistics
    s->AddPerUtFwdFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerUtFwdFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);

    // Other statistics
    s->AddPerUtFwdFeederLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtFwdUserLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnFeederLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerUtRtnUserLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerGwRtnFeederQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnFeederQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatRtnFeederQueuePackets(SatStatsHelper::OUTPUT_SCATTER_FILE);

    s->AddPerGwFwdUserQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatFwdUserQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
    s->AddPerSatFwdUserQueuePackets(SatStatsHelper::OUTPUT_SCATTER_FILE);

    simulationHelper->EnableProgressLogs();
    
    // Schedule queue size logging to start after 1 second and repeat every 1 second
    Simulator::Schedule(Seconds(1.0), &LogQueueSizes);
    
    simulationHelper->RunSimulation();

    return 0;
}