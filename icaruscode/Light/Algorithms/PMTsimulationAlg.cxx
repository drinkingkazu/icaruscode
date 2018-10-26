/**
 * @file    icaruscode/Light/Algorithms/PMTsimulationAlg.cxx
 * @brief   Algorithms for the simulation of ICARUS PMT channels.
 * @date    October 16, 2018
 * @see     `icaruscode/Light/Algorithms/PMTsimulationAlg.h`
 *
 * Implementation file.
 */

// this library header
#include "icaruscode/Light/Algorithms/PMTsimulationAlg.h"

// framework libraries
#include "cetlib_except/exception.h"

// CLHEP libraries
#include "CLHEP/Random/RandFlat.h"
#include "CLHEP/Random/RandGauss.h"
#include "CLHEP/Random/RandExponential.h"

// C++ standard libaries
#include <utility> // std::move()


// -----------------------------------------------------------------------------   
// ---  icarus::opdet::PMTsimulationAlg
// -----------------------------------------------------------------------------   
icarus::opdet::PMTsimulationAlg::PMTsimulationAlg
  (ConfigurationParameters_t const& config)
  : fParams(config)
  , fQE(fParams.QEbase / fParams.larProp->ScintPreScale())
  , fSampling(fParams.timeService->OpticalClock().Frequency())
  , fNsamples(fParams.readoutEnablePeriod * fSampling) // us * MHz cancels out
  , wsp(
    { // PhotoelectronPulseWaveform
      fParams.ADC * fParams.meanAmplitude, // amplitude
      fParams.transitTime,                 // peak time
      riseTimeToRMS(fParams.riseTime),     // sigma left
      riseTimeToRMS(fParams.fallTime)      // sigma right
    },
    fSampling / 1.0e3, // convert frequency into GHz
    6.0                // 6 sdt. dev. of tail should suffice
    )
{

  //  mf::LogDebug("PMTsimulationAlg") << "Sampling = " << fSampling << " MHz." << std::endl;
  
  // shape of single pulse
 
  // Correction due to scalling factor applied during simulation if any
  mf::LogDebug("PMTsimulationAlg") << "PMT corrected efficiency = " << fQE;
    
  if (fQE >= 1.0001) {
    mf::LogWarning("PMTsimulationAlg")
      << "WARNING: Quantum efficiency set in the configuration (QE="
        << fParams.QEbase << ") seems to be too large!"
      "\nThe photon visibility library is assumed to already include a"
        " quantum efficiency of " << fParams.larProp->ScintPreScale() <<
        " (`ScintPreScale` setting of `LArProperties` service)"
        " and here we are requesting a higher one."
      "\nThis configuration is not supported by `PMTsimulationAlg`,"
        " and this simulation will effectively apply a quantum efficiency of "
        << fParams.larProp->ScintPreScale();
      ;
  }
  
  printConfiguration(mf::LogDebug("PMTsimulationAlg") << "PMT simulation configuration:\n");
  // check that the sampled waveform has a sufficiently large range, so that
  // tails are below 10^-3 ADC counts (absolute value)
  wsp.checkRange(1e-3, "PMTsimulationAlg");
   
} // icarus::opdet::PMTsimulationAlg::setup()


// -----------------------------------------------------------------------------   
std::vector<raw::OpDetWaveform> icarus::opdet::PMTsimulationAlg::simulate
  (sim::SimPhotons const& photons)
{
  std::vector<raw::OpDetWaveform> waveforms; // storage of the results
  
  Waveform_t waveform;
  std::vector<unsigned int> PhotoelectronsPerSample;
  CreateFullWaveform(waveform, PhotoelectronsPerSample, photons);
  CreateOpDetWaveforms(photons.OpChannel(), waveform, waveforms);
  return waveforms;
  
} // icarus::opdet::PMTsimulationAlg::simulate()


//------------------------------------------------------------------------------
void icarus::opdet::PMTsimulationAlg::CreateFullWaveform(Waveform_t & waveform,
					std::vector<unsigned int> & PhotoelectronsPerSample,
					sim::SimPhotons const& photons){

    //auto& waveform = fFullWaveforms[opch];
    waveform.resize(fNsamples,fParams.baseline);
    PhotoelectronsPerSample.resize(fNsamples,0U);
    std::unordered_map<unsigned int,unsigned int> peMap;
    // collect the amount of photoelectrons arriving at each tick
    //std::vector<unsigned int> PhotoelectronsPerSample(fNsamples, 0U);
    
    //for(auto const& photons : pmtVector){
    //if(raw::Channel_t(photons.OpChannel())!=opch) continue;
    
    //std::cout << "Creating waveform " << photons.OpChannel() << " " << photons.size() << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    //auto& waveform = fFullWaveforms[photons.OpChannel()];
    //waveform.resize(fNsamples,fBaseline);
    
    for(auto const& ph : photons) {
      if (!KicksPhotoelectron()) continue;
      
      double const mytime = fParams.timeService->G4ToElecTime(ph.Time+fParams.transitTime)-fParams.timeService->TriggerTime()-fParams.triggerOffsetPMT;
      if ((mytime < 0.0) || (mytime >= fParams.readoutEnablePeriod)) continue;
      
      std::size_t const iSample = static_cast<std::size_t>(mytime * fSampling);
      if (iSample >= fNsamples) continue;
      //++PhotoelectronsPerSample[iSample];
      ++peMap[iSample];
    } // for photons
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end-start;
    //std::cout << "\tcollected pes... " << photons.OpChannel() << " " << diff.count() << std::endl;
    start=std::chrono::high_resolution_clock::now();

      // add the collected photoelectrons to the waveform
      //for (std::size_t iSample = 0; iSample < fNsamples; ++iSample) {
    unsigned int nTotalPE [[gnu::unused]] = 0U; // unused if not in `debug` mode
    for(auto const& pe : peMap){
      auto const nPE = pe.second;//PhotoelectronsPerSample[iSample];
      nTotalPE += nPE;
      if (nPE == 0) continue;
      if (nPE == 1) AddSPE(pe.first,waveform);//AddSPE(iSample, waveform); // faster if n = 1
      else AddPhotoelectrons(pe.first, nPE, waveform);//AddPhotoelectrons(iSample, nPE, waveform);
    }
    LOG_TRACE("PMTsimulationAlg") 
      << nTotalPE << " photoelectrons at " << peMap.size()
      << " times in channel " << photons.OpChannel();

      end=std::chrono::high_resolution_clock::now(); diff = end-start;
      //std::cout << "\tadded pes... " << photons.OpChannel() << " " << diff.count() << std::endl;
      start=std::chrono::high_resolution_clock::now();

      if(fParams.ampNoise>0.) AddNoise(waveform);
      if(fParams.darkNoiseRate>0.) AddDarkNoise(waveform);

      end=std::chrono::high_resolution_clock::now(); diff = end-start;
      //std::cout << "\tadded noise... " << photons.OpChannel() << " " << diff.count() << std::endl;
      start=std::chrono::high_resolution_clock::now();

      // Implementing saturation effects;
      // waveform is negative, and saturation is a minimum ADC count
      auto const saturationLevel = fParams.baseline + fParams.saturation*wsp.shape().amplitude();
      std::replace_if(waveform.begin(),waveform.end(),
		      [saturationLevel](float s) -> bool{return s < saturationLevel;},
		      saturationLevel);
      //for (auto& sample: waveform) {
      //if (sample < saturationLevel) sample = saturationLevel;
      //}
      //}
      
      end=std::chrono::high_resolution_clock::now(); diff = end-start;
      //std::cout << "\tadded saturation... " << photons.OpChannel() << " " << diff.count() << std::endl;

  } // CreateFullWaveform()

  std::set<size_t> icarus::opdet::PMTsimulationAlg::CreateBeamGateTriggers() const
  {
    double trig_time;
    std::set<size_t> trigger_locations;
    for(size_t i_trig=0; i_trig<fParams.beamGateTriggerNReps; ++i_trig){
      trig_time = (fParams.timeService->BeamGateTime()-fParams.timeService->TriggerTime())+fParams.beamGateTriggerRepPeriod*i_trig-fParams.triggerOffsetPMT;
      if(trig_time<0 || trig_time>fParams.readoutEnablePeriod) continue;
      trigger_locations.insert(size_t(trig_time*fSampling));
    }
    return trigger_locations;
  }

  std::set<size_t> icarus::opdet::PMTsimulationAlg::FindTriggers(Waveform_t const& wvfm) const
  {
    std::set<size_t> trigger_locations;
    if (fParams.createBeamGateTriggers) trigger_locations = CreateBeamGateTriggers();
    
    short val;
    bool above_thresh=false;

    //next, find all ticks at which we would trigger readout
    for(size_t i_t=0; i_t<wvfm.size(); ++i_t){
      
      val = fParams.pulsePolarity*(short)(wvfm[i_t]-fParams.baseline);

      if(!above_thresh && val>=fParams.thresholdADC){
	above_thresh=true;
	trigger_locations.insert(i_t);
      }
      else if(above_thresh && val<fParams.thresholdADC){
	above_thresh=false;
      } 
      
    }//end loop over waveform   
    return trigger_locations;
  }

//------------------------------------------------------------------------------
void icarus::opdet::PMTsimulationAlg::CreateOpDetWaveforms(raw::Channel_t const& opch,
					  Waveform_t const& wvfm,
					  std::vector<raw::OpDetWaveform>& output_opdets)
  {
    //std::cout << "Finding triggers in " << opch << std::endl;

    std::set<size_t> trigger_locations = FindTriggers(wvfm);

    //std::cout << "Creating opdet waveforms in " << opch << std::endl;

    bool in_pulse=false;
    size_t trig_start=0,trig_stop=wvfm.size();

    auto const pretrigSize = fParams.pretrigSize();
    auto const posttrigSize = fParams.posttrigSize();
    LOG_TRACE("PMTsimulationAlg")
      << "Channel #" << opch << ": " << trigger_locations.size() << " triggers";
    for(size_t i_t=0; i_t<wvfm.size(); ++i_t){

      //if we are at a trigger point, open the window
      if(trigger_locations.count(i_t)==1){

	//if not already in a pulse
	if(!in_pulse){
	  in_pulse=true;
	  trig_start = i_t>pretrigSize ? i_t-pretrigSize : 0;
	  trig_stop  = (wvfm.size()-1-i_t)>posttrigSize ? i_t+posttrigSize : wvfm.size();
	  
	}
	//else, if we are already in a pulse, extend it
	else if(in_pulse){
	  trig_stop  = (wvfm.size()-1-i_t)>posttrigSize ? i_t+posttrigSize : wvfm.size();
	}
      }

      //ok, now, if we are in a pulse but have reached its end, store the waveform
      if(in_pulse && i_t==trig_stop-1){
	output_opdets.emplace_back( raw::TimeStamp_t((trig_start/fSampling + fParams.triggerOffsetPMT)),
				    opch,
				    trig_stop-trig_start );
	output_opdets.back().Waveform().assign(wvfm.begin()+trig_start,wvfm.begin()+trig_stop);
	in_pulse=false;
      }
      
    }//end loop over waveform
  }

  bool icarus::opdet::PMTsimulationAlg::KicksPhotoelectron() const
    { return CLHEP::RandFlat::shoot(fParams.randomEngine) < fQE; } 

  
  void icarus::opdet::PMTsimulationAlg::AddPhotoelectrons(size_t time_bin, unsigned int n, Waveform_t& wave) const {
    
    if (time_bin >= fNsamples) return;
    
    std::size_t const min = time_bin;
    std::size_t const max = std::min(time_bin + wsp.pulseLength(), fNsamples);

    std::transform(wave.begin()+min,wave.begin()+max,wsp.begin(),wave.begin()+min,
		   [n](float a, float b) -> float{return a+n*b;});
		   //addmultiple<n>());

    //for (std::size_t i = min; i < max; ++i) {
    //wave[i] += n * wsp[i-min];
    //}
      
  } // PMTsimulationAlg::AddPhotoelectrons()
  
  void icarus::opdet::PMTsimulationAlg::AddSPE(size_t time_bin, Waveform_t& wave){
     
    if (time_bin >= fNsamples) return;
    
    std::size_t const min = time_bin;
    std::size_t const max = std::min(time_bin + wsp.pulseLength(), fNsamples);

    std::transform(wave.begin()+min,wave.begin()+max,wsp.begin(),wave.begin()+min,std::plus<float>());
    //for (std::size_t i = min; i < max; ++i) {
    //wave[i] += wsp[i-min];
    //}
    
  }
  
  void icarus::opdet::PMTsimulationAlg::AddNoise(Waveform_t& wave){
    
    CLHEP::RandGauss random(*fParams.elecNoiseRandomEngine, 0.0, fParams.ampNoise);
    for(auto& sample: wave) {
      double const noise = random.fire(); //gaussian noise
      sample += noise;
    } // for sample
    
  } // PMTsimulationAlg::AddNoise()
  
  void icarus::opdet::PMTsimulationAlg::AddDarkNoise(Waveform_t& wave)
  {
    if (fParams.darkNoiseRate <= 0.0) return; // no dark noise
    size_t timeBin=0;
    CLHEP::RandExponential random(*(fParams.darkNoiseRandomEngine), (1.0/fParams.darkNoiseRate)*1e9);
    // Multiply by 10^9 since fDarkNoiseRate is in Hz (conversion from s to ns)
    double darkNoiseTime = random.fire();
    while (darkNoiseTime < wave.size()){
      timeBin = (darkNoiseTime);
      AddSPE(timeBin,wave);
      // Find next time to add dark noise
      darkNoiseTime += random.fire();
    }
  }
  


// -----------------------------------------------------------------------------   
// ---  icarus::opdet::PMTsimulationAlgMaker
// -----------------------------------------------------------------------------   
icarus::opdet::PMTsimulationAlgMaker::PMTsimulationAlgMaker
  (Config const& config)
{
  //
  // readout settings
  //
  fBaseConfig.readoutEnablePeriod      = config.ReadoutEnablePeriod();
  fBaseConfig.readoutWindowSize        = config.ReadoutWindowSize();
  fBaseConfig.ADC                      = config.ADC();
  fBaseConfig.baseline                 = config.Baseline();
  fBaseConfig.pulsePolarity            = config.PulsePolarity();
  fBaseConfig.pretrigFraction          = config.PreTrigFraction();
  fBaseConfig.triggerOffsetPMT         = config.TriggerOffsetPMT();
  
  //
  // PMT settings
  //
  fBaseConfig.saturation               = config.Saturation();
  fBaseConfig.QEbase                   = config.QE();
  
  //
  // single photoelectron response
  //
  fBaseConfig.transitTime              = config.TransitTime();
  fBaseConfig.riseTime                 = config.RiseTime();
  fBaseConfig.fallTime                 = config.FallTime();
  fBaseConfig.meanAmplitude            = config.MeanAmplitude();
  
  //
  // dark noise
  //
  fBaseConfig.darkNoiseRate            = config.DarkNoiseRate();
  
  //
  // electronics noise
  //
  fBaseConfig.ampNoise                 = config.AmpNoise();
  
  //
  // trigger
  //
  fBaseConfig.thresholdADC             = config.ThresholdADC();
  fBaseConfig.createBeamGateTriggers   = config.CreateBeamGateTriggers();
  fBaseConfig.beamGateTriggerRepPeriod = config.BeamGateTriggerRepPeriod();
  fBaseConfig.beamGateTriggerNReps     = config.BeamGateTriggerNReps();
  
  //
  // parameter checks
  //
  if (std::abs(fBaseConfig.pulsePolarity) != 1.0) {
    throw cet::exception("PMTsimulationAlg")
      << "Pulse polarity settings can be only +1.0 or -1.0 (got: "
      << fBaseConfig.pulsePolarity << ")\n"
      ;
  } // check pulse polarity
  
} // icarus::opdet::PMTsimulationAlgMaker::PMTsimulationAlgMaker()


//-----------------------------------------------------------------------------
std::unique_ptr<icarus::opdet::PMTsimulationAlg>
icarus::opdet::PMTsimulationAlgMaker::operator()(
  detinfo::LArProperties const& larProp,
  detinfo::DetectorClocks const& detClocks,
  CLHEP::HepRandomEngine& mainRandomEngine, 
  CLHEP::HepRandomEngine& darkNoiseRandomEngine, 
  CLHEP::HepRandomEngine& elecNoiseRandomEngine 
  ) const
{
  // set the configuration 
  auto params = fBaseConfig;
  
  // set up parameters
  params.larProp = &larProp;
  params.timeService = &detClocks;
  
  params.randomEngine = &mainRandomEngine;
  params.darkNoiseRandomEngine = &darkNoiseRandomEngine;
  params.elecNoiseRandomEngine = &elecNoiseRandomEngine;

  return std::make_unique<PMTsimulationAlg>(params);
   
} // icarus::opdet::PMTsimulationAlgMaker::create()
