# Dyanmic BH enabled sns-3

This repository contains some changes on top of [sns-3 repository](https://github.com/sns3/sns3-satellite) to run dynamic beam hopping examples presented in:

> *"The Impact of Demand Forecasting on Delay and Jitter in DVB-Based Beam-Hopping LEO Networks"*

The same license of the official sns-3 applies to this repository.

## Directory Structure

The supporter repositories must be cloned inside the `contrib/` folder of your base `ns` directory. Your final folder structure should look exactly like this:

```text
your_folders/
└── ns/                     # Base Repository
    └── contrib/
        ├── magister-stats/       # Supporter Repository
        ├── satellite/            # Supporter Repository
        └── traffic/              # Supporter Repository     
```

To simulate dynamic BH example you can follow the following:

# A. Clone the base ns-3 repository, checkout go to contrib
```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git ns
cd ns && git checkout ea50b72ab79b1ec5912c66d5b384effa829f18bc
cd contrib
```


## --- 1. Clone and Checkout custom 'satellite' ---
[Following](https://github.com/YektaDemirci/sns3_dynamic_bh.git) is the sns-3 repo that supports dynamic BH and built on top of official [sns-3 repository](https://github.com/sns3/sns3-satellite)

```bash
git clone https://github.com/YektaDemirci/sns3_dynamic_bh.git satellite
```

## --- 2. Clone and Checkout 'traffic' ---
```bash
git clone https://github.com/sns3/traffic.git traffic
cd traffic && git checkout 2710ed71c2cba9d92940bdf53ad85159504329d9 && cd ..
```

## --- 3. Clone and Checkout 'magister-stats' ---
```bash
git clone https://github.com/sns3/stats.git magister-stats
cd magister-stats && git checkout 003f6a29ce74808d0b2579fd16a9718f793f5bfe && cd ../..
```

## --- 4. Compilation
Once you have these, you can compile the source codes as described by [SNS-3 CMake](https://github.com/sns3/sns3-satellite#cmake). I had cmake version 3.28.3 while compiling the source codes.

You need to be on /ns folder:
```bash
./ns3 clean
./ns3 configure --build-profile=optimized --enable-examples --enable-tests
./ns3 build
```

Once the compilation is successful you can get the input data by:
```bash
cd contrib/satellite/
git submodule update --init --recursive
```

Then you need to copy the following two data folders to support dynamic BH scenarios. They are provided in [additional support data for dynamic BH](https://github.com/YektaDemirci/forecastDVBperspective)

* i) leo-tlst3-beam-hopping
* ii) SatAntennaGain72BeamsShifted

```text
your_folders/
└── ns/                     # Base Repository
    └── contrib/
        └── satellite/
            └── data/
                └── scenarios/
                    └──leo-tlst3-beam-hopping # Copy here
```                 

```text
your_folders/
└── ns/                     # Base Repository
    └── contrib/
        └── satellite/
            └── data/
                └── additional-input/
                    └──SatAntennaGain72BeamsShifted # Replace this folder
```

Then, at ns/ you should be able to run the provided simulation examples, for instance:

```bash
./ns3 run sat-fwd-link-beam-hopping-example-dynamic -- --simTime=3.01 --planSuperframes=15  --users=sym   --scheduler=fixed   --userBw=330e6   --shape55=1.04   --shape56=1.04   --shape57=1.04
```