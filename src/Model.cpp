//
//  Model.cpp
//  transfil
//
//  Created by Paul Brown on 27/01/2017.
//  Copyright © 2017 Paul Brown. All rights reserved.
//

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "Model.hpp"
#include "Population.hpp"
#include "RecordedPrevalence.hpp"
#include "Scenario.hpp"
#include "ScenariosList.hpp"
#include "Vector.hpp"
#include "Worm.hpp"

extern bool _DEBUG;
extern Statistics stats;

void Model::runScenarios(ScenariosList &scenarios, Population &popln,
                         Vector &vectors, Worm &worms, int replicates,
                         double timestep, int index, int outputEndgame,
                         int outputEndgameDate, bool outputNTDMC,
                         int outputNTDMCDate, int reduceImpViaXml,
                         std::string randParamsfile, std::string RandomSeedFile,
                         std::string RandomCovPropFile, std::string opDir) {

  std::cout << std::endl
            << "Index " << index << " running " << scenarios.getName()
            << " with " << scenarios.getNumScenarios() << " scenarios"
            << std::endl;

  std::cout << std::unitbuf;
  std::cout << "Progress:  0%";

  Output currentOutput(scenarios.getBaseYear());
  currentOutput.saveRandomNames(printSeedName());
  currentOutput.saveRandomNames(popln.printRandomVariableNames());
  currentOutput.saveRandomNames(
      vectors.printRandomVariableNames()); // names of random vars to be printed
  currentOutput.saveRandomNames(worms.printRandomVariableNames());

  dt = timestep;

  scenarios.openFilesandPrintHeadings(index, currentOutput);

  std::vector<double> k_vals;
  std::vector<double> v_to_h_vals;
  std::vector<double> aImp_vals;
  std::vector<double> wPropMDA;
  std::vector<unsigned long int> seeds;
  // we read in the entries of the random seed file. a different value will be
  // used for each set of parameters
  readSeedsFromFile(seeds, unsigned(replicates), RandomSeedFile);

  std::vector<double> cov_props;
  // we read in the entries of the random cov_props file. a different value will
  // be used for each set of parameters
  readCovPropFromFile(cov_props, unsigned(replicates), RandomCovPropFile);

  for (int rep = 0; rep < replicates; rep++) {
    // Read the seed from the seeds vector if it has been generated
    // othewise we set a random seed
    unsigned long int rseed;
    if (seeds.size() > 0) {
      rseed = seeds[rep];
      stats.set_seed(rseed);
    } else {
      rseed = std::chrono::system_clock::now().time_since_epoch().count();
      stats.set_seed(rseed);
    }
    // Read the value to multiply the MDA's by if this has been supplied
    // othewise we set this to 1
    double cov_prop;
    if (cov_props.size() > 0) {
      cov_prop = cov_props[rep];
    } else {
      cov_prop = 1.0;
    }
    getRandomParametersMultiplePerLine(rep + 1, k_vals, v_to_h_vals, aImp_vals,
                                       wPropMDA, unsigned(replicates),
                                       randParamsfile);
    currentMonth = 0;
    popln.clearSavedMonths();
    vectors.clearSavedMonths();
    currentOutput.initialise(); // delete previous replicate

    // functions temporarily altered to allow random value to be passed in

    std::string distType = stats.selectDistribType();
    // create a new host population with random size, ages and compliance p vals
    // and bite risk. Generates new value for k and aImp
    // popln.initHosts(distType, k_vals[rep], aImp_vals[rep]);
    popln.initHosts(distType, k_vals[0], aImp_vals[0]);
    // generate vector to host ratio value and reset L3 to initial value
    // vectors.reset(distType, v_to_h_vals[rep]);
    vectors.reset(distType, v_to_h_vals[0]);

    // tmp fix to set wPropMDA
    // worms.reset(wPropMDA[rep]);
    worms.reset(wPropMDA[0]);

    // save these values for printing later
    currentOutput.clearRandomValues();
    // MUST be cvalled i nsame order as saveRandomNames above
    currentOutput.saveSeedValue(rseed);
    currentOutput.saveRandomValues(popln.printRandomVariableValues());
    currentOutput.saveRandomValues(vectors.printRandomVariableValues());
    currentOutput.saveRandomValues(worms.printRandomVariableValues());

    // baseline prevalence
    PrevalenceEvent pe = PrevalenceEvent(
        popln.getMinAgePrev(), scenarios.getExtraMinAge(),
        scenarios.getExtraMaxAge(),
        scenarios.getOutputtMethod()); // default age range and method to output
                                       // at end of burn in
    burnIn(popln, vectors, worms, currentOutput,
           &pe); // should be at least 100 years
    // Run each scenario
    for (unsigned s = 0; s < scenarios.getNumScenarios(); s++) {

      Scenario &sc = scenarios[s];

      if (_DEBUG)
        std::cout << std::endl
                  << sc.getName() << " starts month " << sc.getStartMonth()
                  << std::endl;

      if (sc.getStartMonth() != currentMonth) {

        // reset to the start of this month
        currentMonth = sc.getStartMonth();
        // reset to the start of currentMonth
        popln.resetToMonth(currentMonth);   // worms and aImp
        vectors.resetToMonth(currentMonth); // L3

        // delete any results with a month >= to this month
        currentOutput.resetToMonth(currentMonth); // MDA and prev
      }

      // evolve, saving any specified months along the way
      for (int y = 0; y < sc.getNumMonthsToSave(); y++) {
        evolveAndSave(y, popln, vectors, worms, sc, currentOutput, rep, k_vals,
                      v_to_h_vals, popln.getUpdateParams(), outputEndgame,
                      outputEndgameDate, outputNTDMC, outputNTDMCDate,
                      reduceImpViaXml, opDir, cov_prop);
      }

      // done for this scenario, save the prevalence values for this replicate
      if (!_DEBUG)
        sc.printResults(rep, currentOutput, popln);

      if (_DEBUG)
        popln.printMDAHistory();

    } // end of each scenario
    if (!_DEBUG) {
      std::cout << "\b\b\b\b";
      std::cout << std::setw(3) << int(rep * 100 / replicates) << "%";
    }
  } // end for each rep

  scenarios.closeFiles();
  // finished
}

bool Model::shouldReduceImportationViaPrevalance(
    int reduceImpViaXml, int t, int switchImportationReducingMethodTime) {
  // function to check if we should reduce the importation rate via checking how
  // the prevalence has changed over time. the alternative to this is via
  // specification in the XML scenario files.
  if ((reduceImpViaXml == 0) || (t >= switchImportationReducingMethodTime)) {
    return true;
  } else {
    return false;
  }
}

void Model::burnIn(Population &popln, Vector &vectors, const Worm &worms,
                   Output &currentOutput, PrevalenceEvent *pe) {

  // burn in period. Don't need to worry about drugs
  // just save final state

  int steps = 12 * std::max(100, popln.getMaxAge()) /
              dt; // one step is 1 * dt months, run for 100 years, Must be at
                  // least maxAge

  for (int i = 0; i < steps; i++) {

    // updates number of worms in each hosts and increments host age
    popln.evolve(dt, vectors, worms);

    // update larval density in the vector population according to new mf levels
    // in host polution
    vectors.updateL3Density(popln, worms);
  }

  // these are initial conditions for start of month zero
  popln.saveCurrentState(0, "burn-in"); // worms and importation rate. Scenario
                                        // name just needed for debugging
  vectors.saveCurrentState(0);          // larval density

  RecordedPrevalence prevalence = popln.getPrevalence(
      pe); // prev measured before mda done to kill mf in hosts
  currentOutput.saveMonth(-1, popln, pe, prevalence);
}

void Model::evolveAndSave(int y, Population &popln, Vector &vectors,
                          Worm &worms, Scenario &sc, Output &currentOutput,
                          int rep, std::vector<double> &k_vals,
                          std::vector<double> &v_to_h_vals, int updateParams,
                          int outputEndgame, int outputEndgameDate,
                          bool outputNTDMC, int outputNTDMCDate,
                          int reduceImpViaXml, std::string opDir,
                          double cov_prop) {

  // advance to the next target month
  std::string folderName = opDir;
  std::string MDAType;

  int time_to_reduce_importation_rate =
      -1; // this is for when we want to reduce the importation rate via
          // checking the prevalence post and MDA. This will be set to 6 months
          // after the MDA occurs when the MDA is applied.
  int paramIndex = 0;
  int targetMonth = sc.getMonthToSave(y); // simulate to start of this month
  double mfprev = 1; // variable to check prevalence of mf for survey
  // double icprev = 1; //variable to check prevalence of ic for survey
  int numMDADoSurvey = popln.firstTASNumMDA;
  int sampleSize = popln.getSampleSize();
  // set the outputEndgameDate to be relative to year 2000.
  // This should be updated when we automatically get the baseYear of the
  // simulations from the scenario file.
  int BASEYEAR = 2000;
  outputEndgameDate = (outputEndgameDate - BASEYEAR) * 12;

  int outputNTDMCDateFromYear = (outputNTDMCDate - BASEYEAR) * 12;

  int popSize = popln.getSizeOfPop();
  double mfprev_aimp_old =
      popln.getMFPrev(sc, 0, 0, outputEndgameDate, rep, popSize, folderName);
  double mfprev_aimp_new = 0;
  int changeSensSpec = 0;
  int changeNeverTreat = 0;
  // int maxAge = popln.getMaxAge();
  int neededTASPass =
      3; // number of times TAS must be passed to reached WHO target
         // (https://www.who.int/publications/i/item/9789241501484)
  // int outputTime = floor(currentMonth/12);
  // int outputTime = 0;
  int LymphodemaTotalWorms = popln.getLymphodemaTotalWorms();
  int HydroceleTotalWorms = popln.getHydroceleTotalWorms();
  double LymphodemaShape = popln.getLymphodemaShape();
  double HydroceleShape = popln.getHydroceleShape();

  // Only initalize outputs if we are at the start of a simulation, when y==0
  // (rather than reinitializing for a scenario that has already started)
  if ((outputEndgame == 1) && (y == 0)) {
    sc.InitIHMEData(rep, folderName);
    sc.InitPreTASData(rep, folderName);
    sc.InitTASData(rep, folderName);
  }
  // Only initalize outputs if we are at the start of a simulation, when y==0
  // (rather than reinitializing for a scenario that has already started)
  if ((outputNTDMC == true) && (y == 0)) {
    sc.InitNTDMCData(rep, folderName);
  }

  int vec_control = 0;

  int minAge;
  int maxAge = popln.returnMaxAge();
  int donePreTAS = 0;
  int doneTAS = 0;
  // indicator if we should do the MDA when the MDA is called.
  // This will be switched to false if preTAS is passed, then the MDA function
  // will be called, but will not be done. We will still get the output of the
  // MDA showing that no people were treated this year. This is to keep the
  // output of MDA's constant so that we can combine different runs even if they
  // have different numbers of MDA's performed.
  popln.DoMDA = true;
  int previousMDAyear =
      -1; // we want to keep track of the number of MDA rounds per year, so keep
          // track of the time of the last MDA. Initialize this at -1 as no
          // MDA's will take place then, so we will update correctly for first
          // MDA
  int roundNumber =
      1; // initialize an integer to track which round of MDA in a year is being
         // done and will be recorded in the IHME output
  for (int q = 0; q < popln.sensSpecChangeCount; q++) {
    if (popln.sensSpecChangeName[q] == sc.getName()) {
      changeSensSpec = 1;
    }
  }
  for (int q = 0; q < popln.neverTreatChangeCount; q++) {
    if (popln.neverTreatChangeName[q] == sc.getName()) {
      changeNeverTreat = 1;
    }
  }

  for (int t = currentMonth; t < targetMonth; t += dt) {

    paramIndex = t / 12;
    // if we are updating the k and v_to_h params, then do so if the time is
    // right to do so
    if ((updateParams) && (t % 12 == 0) &&
        (paramIndex <= (k_vals.size() - 1))) {
      popln.updateKVal(k_vals[paramIndex]);
      vectors.updateVtoH(v_to_h_vals[paramIndex]);
    }

    PrevalenceEvent *outputPrev = sc.prevalenceDue(
        t); // defines min age of host to include and method ic/mf
    MDAEvent *applyMDA = sc.treatmentDue(t);
    // at the beginning of every year we record the prevalence of the
    // population, along with the number of people in each age group and the
    // sequelae prevalence in each age group. If it is earlier than the first
    // year we want to do the endgame output for, then don't do this.
    if ((t % 12 == 0) && (outputEndgame == 1) && (t >= outputEndgameDate)) {
      sc.writePrevByAge(popln, t, rep, folderName);

      sc.writeNumberByAge(popln, t, rep, folderName, "not survey");
      sc.writeSequelaeByAge(popln, t, LymphodemaTotalWorms, LymphodemaShape,
                            HydroceleTotalWorms, HydroceleShape, rep,
                            folderName);
      popln.getIncidence(sc, t, rep, folderName);
      sc.writeSurveyByAge(popln, t, popln.preTAS_Pass, popln.TAS_Pass, rep,
                          folderName);
    }

    if ((t % 12 == 0) && (outputNTDMC == true) &&
        (t >= outputNTDMCDateFromYear)) {
      sc.writeRoadmapTarget(popln, t, rep, popln.DoMDA, popln.TAS_Pass,
                            neededTASPass, folderName);
    }

    // If we haven't done a survey this year we still want to output this fact
    // for endgame. This is because the decision to do the survey is dynamic and
    // may occur in some simulations but not in others. to make sure that
    // outputs for different simulations will always have the same number of
    // lines, we will output for each year, even if it is just a bunch of 0's.
    // If it is earlier than the first year we want to do the endgame output
    // for, then don't do this.
    if (((t + 1) % 12 == 0) && (outputEndgame == 1) &&
        (t >= outputEndgameDate)) {

      if (donePreTAS == 0) {

        int year = (t + 1) / 12 + BASEYEAR - 1;
        sc.writeEmptySurvey(year, maxAge, rep, "PreTAS survey", folderName);
        sc.writeNumberByAge(popln, t, rep, folderName, "PreTAS survey");
      }
      donePreTAS = 0;

      if (doneTAS == 0) {

        int year = (t + 1) / 12 + BASEYEAR - 1;
        sc.writeEmptySurvey(year, maxAge, rep, "TAS survey", folderName);
        sc.writeNumberByAge(popln, t, rep, folderName, "TAS survey");
      }
      doneTAS = 0;
    }
    // if we give values externally to reduce the importation rate, then do so
    // here. This will apply up until the time at which we want to switch the
    // method for reducing the importation rate to use the within simulation
    // calculation based on prevalence post an MDA. If not included in the XML
    // file for the scenario the time for this will be long after the end of the
    // simulation, so we will never switch to the other method.
    if (!shouldReduceImportationViaPrevalance(
            reduceImpViaXml, t, popln.switchImportationReducingMethodTime) &&
        (t % 12 == 0)) {
      sc.updateImportationRate(popln, t);
    }
    sc.updateBedNetCoverage(popln, t);

    // updates number of worms in each hosts and increments host age
    popln.evolve(dt, vectors, worms); // simulates to the end of month t

    // update larval density in the vector population according to new mf levels
    // in host polution
    vectors.updateL3Density(popln, worms);

    RecordedPrevalence prevalence;

    if (outputPrev) // use alternative for of prevalence function as its
                    // required to return 2 values, but these must be calculated
                    // at the same time
      prevalence = popln.getPrevalence(
          outputPrev); // prev measured before mda done to kill mf in hosts

    // snippet to perform a preTAS survey
    if (t == popln.preTASSurveyTime) {

      popln.preTAS_Pass = popln.PreTASSurvey(
          sc, outputEndgame, t, outputEndgameDate, rep, folderName);
      if ((outputEndgame == 1) && (t >= outputEndgameDate)) {
        sc.writeNumberByAge(popln, t, rep, folderName, "PreTAS survey");
      }

      donePreTAS = 1;
      if (popln.preTAS_Pass == 1) {
        // if we pass the preTAS survey then we set a time for the TASsurvey
        // we also stop doing MDA
        popln.TASSurveyTime = t;
        popln.DoMDA = false;
      } else {
        popln.preTASSurveyTime = t + popln.interSurveyPeriod;
        popln.DoMDA = true;
      }
    }
    // snippet to perform a TAS survey

    if (t == popln.TASSurveyTime) {
      int TAS_Pass_ind =
          popln.TASSurvey(sc, t, outputEndgameDate, rep, folderName);
      if ((outputEndgame == 1) && (t >= outputEndgameDate)) {
        sc.writeNumberByAge(popln, t, rep, folderName, "TAS survey");
      }
      doneTAS = 1;
      popln.TAS_Pass += TAS_Pass_ind;
      if (TAS_Pass_ind == 0) {
        // if failed, then reset TAS_PAss to 0 and set a time to do another
        // preTAS survey also switch back on MDA's
        popln.TAS_Pass = 0;
        popln.preTASSurveyTime = t + popln.interSurveyPeriod;
        popln.TASSurveyTime = t + popln.interSurveyPeriod;
        popln.DoMDA = true;
      } else if (popln.TAS_Pass == neededTASPass) {
        // if we have passed a sufficient number of times, then make it so we
        // won't do any more TAS surveys
        popln.TASSurveyTime = 99999999;
      } else if (popln.TAS_Pass < neededTASPass) {
        // if we have passed the survey, but need to pass more, then set a time
        // for the next TAS survey
        popln.TASSurveyTime = t + popln.interSurveyPeriod;
        if (vec_control == 0) {
          vec_control = 1;
        }
      }
    }

    // std::cout << applyMDA->getMonth() << std::xendl;
    if (applyMDA) {
      MDAType = popln.returnMDAType(applyMDA);
      minAge = popln.returnMinAgeMDA(applyMDA);
      double coverage_multiplier =
          multiplierForCoverage(t, cov_prop, popln.removeCoverageReduction,
                                popln.removeCoverageReductionTime,
                                popln.graduallyRemoveCoverageReduction);
      double cov = applyMDA->getCoverage() * coverage_multiplier;
      double rho = applyMDA->getCompliance();
      // if we this is the first MDA, then we need to initialise the Probability
      // of treatment for each person
      if (popln.prevCov == -1) {
        popln.initPTreat(cov, rho);
        popln.prevCov = cov;
        popln.prevRho = rho;
      }
      // if the MDA parameters have changed then we need to update the
      // probability of treatment for each person
      if ((popln.prevCov != applyMDA->getCoverage() * coverage_multiplier) ||
          (popln.prevRho != applyMDA->getCompliance())) {
        popln.checkForZeroPTreat(popln.prevCov, popln.prevRho);
        popln.editPTreat(cov, rho);
        popln.prevCov = cov;
        popln.prevRho = rho;
      }
      // check for anyone with 0 probability of treatment, as these will be
      // people who have not had this value initialised
      popln.checkForZeroPTreat(cov, rho);

      // if this this the first MDA then if the NoMDALowMF indicator is 1 then
      // we need to check the MF prevalence in the population as if this is low
      // then we will not begin MDA. If the indicator is not 1 then we will do
      // MDA even with low MF prevalence this uses the sampleSize input as this
      // would be assessed via a survey
      if (popln.totMDAs == 0) {
        if (popln.getNoMDALowMF() == 1) {
          mfprev = popln.getMFPrev(sc, 0, t, outputEndgameDate, rep, sampleSize,
                                   folderName);
          if (mfprev <= popln.MFThreshold) {
            popln.DoMDA = false;
          }
        }
      }

      // record prevalence before the MDA so that we can later assess the
      // decrease in prevalence and reduce the importation inline with this
      // decrease

      // this uses the whole population to get its value as it is used for an
      // intrinsic property of the population
      mfprev_aimp_old = popln.getMFPrev(sc, 0, t, outputEndgameDate, rep,
                                        popSize, folderName);

      int year = t / 12 + 2000;
      if (year == previousMDAyear) {
        roundNumber += 1;
      } else {
        roundNumber = 1;
      }
      previousMDAyear = year;
      // apply the MDA. If popln.DoMDA = false, then we call this function, but
      // don't do the MDA, we just write to a file showing that no people were
      // treated.
      popln.ApplyTreatmentUpdated(applyMDA, worms, sc, t, roundNumber,
                                  outputEndgameDate, rep, popln.DoMDA,
                                  outputEndgame, folderName);
      time_to_reduce_importation_rate = t + 6;

      popln.totMDAs += 1;

      if (popln.totMDAs == numMDADoSurvey) {
        // following the document at
        // https://www.who.int/publications/i/item/9789241501484 the pre TAS
        // survey must be at least 6 months after the 5th effective MDA. We also
        // do not want the surveys to begin too early into the simulation, as
        // the first survey was done around 2012, so we don't want surveys to
        // begin as early as the above condition is passed in some cases. Hence
        // we set the time for the pre TAS survey to be the maximum of a
        // specified date given by popln.getSurveyStartDate() and t +
        // minNumberMonthsBeforeSurvey, which is minNumberMonthsBeforeSurvey
        // from now. We will set the time for the TAS survey if the pre TAS
        // survey is passed.
        int minNumberMonthsBeforeSurvey = 6;
        popln.preTASSurveyTime = std::max(popln.getSurveyStartDate(),
                                          t + minNumberMonthsBeforeSurvey);
      }
    }

    // if it is the time to potentially reduce importation using the simulation
    // rather than externally input values, then do so
    // the popln.switchImportationReducingMethodTime value gives the time for
    // which we will switch to this method rather than using the XML file. This
    // is needed for when we are looking at the future, since the specification
    // within the xml file is based on map data of the progression of LF over
    // time and hence for the future, we will not have any data to use here.
    if (shouldReduceImportationViaPrevalance(
            reduceImpViaXml, t, popln.switchImportationReducingMethodTime) &&
        t == time_to_reduce_importation_rate) {
      mfprev_aimp_new = popln.getMFPrev(sc, 0, t, outputEndgameDate, rep,
                                        popSize, folderName);
      if (mfprev_aimp_old > mfprev_aimp_new) {
        popln.aImp = popln.aImp * mfprev_aimp_new / mfprev_aimp_old;
      }
      mfprev_aimp_old = popln.getMFPrev(sc, 0, t, outputEndgameDate, rep,
                                        popSize, folderName);
    }

    if (t < popln.getNeverTreatChangeTime()) {
      popln.neverTreatToOriginal();
    }

    if (t >= popln.getNeverTreatChangeTime()) {
      if (changeNeverTreat == 1) {
        popln.changeNeverTreat();
        //  std::cout << sc.getName()<< " change never treat " << std::endl;
      }
    }

    if (t < popln.getICTestChangeTime()) {
      popln.ICTestToOriginal();
    }

    if (t >= popln.getICTestChangeTime()) {
      if (changeSensSpec == 1) {
        popln.changeICTest();
        // std::cout << sc.getName()<< " change sens spec" << std::endl;
      }
    }

    // if ((t%12==0)){
    //     currentOutput.saveMonth(t, popln, outputPrev, prevalence, applyMDA);
    // }

    if (outputPrev || applyMDA) {
      currentOutput.saveMonth(t, popln, outputPrev, prevalence, applyMDA);
    }

    if (_DEBUG && applyMDA)
      std::cout << applyMDA->getType() << " month " << currentMonth
                << ", MDA at " << applyMDA->getCoverage() << " coverage"
                << std::endl;

    // next t
  }
  popln.neverTreatToOriginal();
  // done
  currentMonth = targetMonth;
  if (y < (sc.getNumMonthsToSave() - 1)) { // not finished this scenario
    popln.saveCurrentState(
        currentMonth,
        sc.getName()); // worms and importation rate. Scenario
                       // name just needed for debugging
    vectors.saveCurrentState(currentMonth); // larval density

    if (_DEBUG)
      std::cout << sc.getName() << " saving month " << currentMonth
                << std::endl;
  }
}

void Model::getRandomParameters(int index, std::vector<double> &k_vals,
                                std::vector<double> &v_to_h_vals,
                                std::vector<double> &aImp_vals,
                                std::vector<double> &wProp_vals,
                                unsigned replicates, std::string fname) {

  std::ifstream infile(fname, std::ios_base::in);

  if (!infile.is_open()) {
    std::cout << "Error in Model::runScenarios. Cannot read file " << fname
              << std::endl;
    exit(1);
  }

  std::string line;

  while (getline(infile, line)) {

    std::istringstream linestream(line);
    double k = -1.0, v_to_h = -1.0, aImp = -1.0, wProp = -1.0;

    linestream >> v_to_h >> k >> aImp >> wProp;

    if (k < 0 || v_to_h < 0 || aImp < 0) {
      std::cout << "Error in Model::runScenarios. Error reading file " << fname
                << std::endl;
      exit(1);
    }

    k_vals.push_back(k);
    aImp_vals.push_back(aImp);
    v_to_h_vals.push_back(v_to_h);
    wProp_vals.push_back(wProp);
  }

  infile.close();

  if ((k_vals.size() < replicates) || (v_to_h_vals.size() < replicates) ||
      (aImp_vals.size() < replicates) || (wProp_vals.size() < replicates)) {
    std::cout << "Error in Model::runScenarios. " << fname
              << " is too short for " << replicates << "replicates"
              << std::endl;
    exit(1);
  }
}

double Model::multiplierForCoverage(int t, double cov_prop,
                                    int removeCoverageReduction,
                                    int removeCoverageReductionTime,
                                    int graduallyRemoveCoverageReduction) {

  // if we want to gradually remove the reduction in coverage then
  // return a scaled reduction parameter
  if (graduallyRemoveCoverageReduction == 1) {
    if (t < removeCoverageReductionTime)
      return ((1 - cov_prop)) * (double(t) / removeCoverageReductionTime) +
             cov_prop;
    else
      return 1;
  } else if (removeCoverageReduction == 1) {
    // if we will remove the coverage instantly, then return the
    // coverage reduction parameter until the removeCoverageReductionTime
    if (t < removeCoverageReductionTime)
      return cov_prop;
    else
      return 1;
  } else
    // if we don't want to do either of these, then just return the coverage
    // reduction parameter
    return cov_prop;
}

void Model::ProcessLine(const std::string &line, std::vector<double> &k_vals,
                        std::vector<double> &v_to_h_vals,
                        std::vector<double> &aImp_vals,
                        std::vector<double> &wProp_vals) {
  std::istringstream iss(line);
  double value;
  int arraySize = k_vals.size();
  int k = 0;
  for (int j = 1; iss >> value; ++j) {

    if (j % 4 == 1) {
      if (arraySize > 0) {
        v_to_h_vals[k] = value;
      } else {
        v_to_h_vals.push_back(value);
      }
    } else if (j % 4 == 2) {
      if (arraySize > 0) {
        k_vals[k] = value;
      } else {
        k_vals.push_back(value);
      }
    } else if (j % 4 == 3) {
      if (arraySize > 0) {
        aImp_vals[k] = value;
      } else {
        aImp_vals.push_back(value);
      }
    } else if (j % 4 == 0) {
      if (arraySize > 0) {
        wProp_vals[k] = value;
        k++;
      } else {
        wProp_vals.push_back(value);
      }
    }
  }
  if (k != arraySize) {
    std::cout << " Error" << std::endl;
    std::cerr << "number of input parameters has changed" << std::endl;
    // exit(1);
  }
}

void Model::getRandomParametersMultiplePerLine(
    int index, std::vector<double> &k_vals, std::vector<double> &v_to_h_vals,
    std::vector<double> &aImp_vals, std::vector<double> &wProp_vals,
    unsigned replicates, std::string fname) {

  // Replace with the desired line number

  std::ifstream file(fname);
  std::string line;

  if (!file.is_open()) {
    std::cerr << "Failed to open file: " << fname << std::endl;
  }

  for (int i = 1; i <= index; ++i) {
    if (!std::getline(file, line)) {
      std::cerr << "Error reading line " << index << " from file: " << fname
                << std::endl;
    }
  }

  ProcessLine(line, k_vals, v_to_h_vals, aImp_vals, wProp_vals);
}

void Model::readSeedsFromFile(std::vector<unsigned long int> &seeds,
                              unsigned replicates, std::string fname) {
  // We retrieve the random seeds from the input seed file. The line on which
  // the seed is on will correspond to the set of parameters on the same line of
  // the input parameters file. If there is no seed file input we don't do
  // anything and will later set the seed randomly.
  if (fname.length() > 0) {
    std::ifstream infile(fname, std::ios_base::in);
    unsigned long int seed;
    if (!infile.is_open()) {
      std::cout << "Error in getting seeds. Cannot read file " << fname
                << std::endl;
      exit(1);
    }
    std::string line;
    while (getline(infile, line)) {
      std::istringstream linestream(line);
      linestream >> seed;
      seeds.push_back(seed);
    }
    // if we haven't input enough seeds based on how many runs we want to make
    // then we will abort the run as at some point we will run out of seeds and
    // the simulations will crash.
    if (seeds.size() < replicates) {
      std::cout << "Error in Model::runScenarios. " << fname
                << " is too short for " << replicates << " replicates"
                << std::endl;
      exit(1);
    }
    infile.close();
  }
}

void Model::readCovPropFromFile(std::vector<double> &cov_props,
                                unsigned replicates, std::string fname) {
  // We retrieve the random MDA shrinkage value from the input covproporation
  // file. The line on which the value is on will correspond to the set of
  // parameters on the same line of the input parameters file. If there is no
  // covproporation file input we set it to 1.
  //
  if (fname.length() > 0) {
    std::ifstream infile(fname, std::ios_base::in);
    double cov_prop;
    if (!infile.is_open()) {
      std::cout << "Error in getting shrinkage. Cannot read file " << fname
                << std::endl;
      exit(1);
    }
    std::string line;
    while (getline(infile, line)) {
      std::istringstream linestream(line);
      linestream >> cov_prop;
      cov_props.push_back(cov_prop);
    }
    // if we haven't input enough values based on how many runs we want to make
    // then we will abort the run as at some point we will run out of values and
    // the simulations will crash.
    if (cov_props.size() < replicates) {
      std::cout << "Error in Model::runScenarios. " << fname
                << " is too short for " << replicates << " replicates"
                << std::endl;
      exit(1);
    }
    infile.close();
  }
}

std::vector<std::string> Model::printSeedName() const {

  // outputs "seed" as a column title for fitting output file

  std::vector<std::string> names = {"seed"};
  return names;
}
