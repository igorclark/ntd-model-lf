// pointer to classes example

#include <iostream>
#include <sstream>
#include <string.h>
#include <string>
#include <sys/time.h>

#include "Model.hpp"
#include "Population.hpp"
#include "ScenariosList.hpp"
#include "Statistics.hpp"
#include "Vector.hpp"
#include "Worm.hpp"
#include "tinyxml.h"

/*
 to compile on mac

 g++ -I. -I./tinyxml -I/usr/local/include  -L/usr/local/lib  -Wall -O3
 -std=c++11 -stdlib=libc++  -lm -lgsl -lgslcblas main.cpp ScenariosList.cpp
 Scenario.cpp Population.cpp Host.cpp MDAEvent.cpp Model.cpp Output.cpp
 Vector.cpp Worm.cpp Statistics.cpp BedNetEvent.cpp PrevalenceEvent.cpp
 tinyxml/tinystr.cpp tinyxml/tinyxmlparser.cpp tinyxml/tinyxmlerror.cpp
 tinyxml/tinyxml.cpp -o transfil

 on nero


 g++ -I. -I./tinyxml -I/usr/include  -L/usr/lib64  -Wall -O3 -std=c++11  -lm
 -lgsl -lgslcblas main.cpp ScenariosList.cpp Scenario.cpp Population.cpp
 Host.cpp Model.cpp Output.cpp Vector.cpp Worm.cpp Statistics.cpp MDAEvent.cpp
 BedNetEvent.cpp PrevalenceEvent.cpp ImportationRateEvent.cpp
 RecordedPrevalence.cpp tinyxml/tinystr.cpp tinyxml/tinyxmlparser.cpp
 tinyxml/tinyxmlerror.cpp tinyxml/tinyxml.cpp -o transfil


 */

bool _DEBUG = false;
Statistics stats;

// Exclude the test when building the `model` library
// used for testing (as CTest provides the main method)
#ifndef DISABLE_MAIN_METHOD
int main(int argc, char **argv) {

  if (argc < 2) {

    std::cout
        << "transfil index -s <scenarios_file> -n <pop_file> -p "
           "<random_parameters_file> -r <replicates=1000> -t <timestep=1> -o "
           "<output_directory=\"./\"> -g <random_seed=1> -e <output_endgame=1> "
           "-x <reduce_imp_via-xml=0> -D <outputEndgameDate=2000>"
        << std::endl;
    return 1;
  }

  struct timeval tv1, tv2;
  gettimeofday(&tv1, NULL);

  int replicates = 0;
  double dt = 1.0;
  std::string popFile;
  std::string randParamsfile("");
  std::string scenariosFile("");
  std::string opDir("");
  std::string RandomSeedFile("");
  std::string CoverageReductionFile("");

  // initialize random seed value, whether the endgame output will be done
  // and whether the reduction in importation rate should be done via the
  // xml file rather than impact of MDA on the prevalence
  int outputEndgame = 1;
  int outputEndgameDate = 2000;
  bool outputNTDMC = true;
  int outputNTDMCDate = 2000;
  int reduceImpViaXml = 0;
  int NTDMC = 1;
  int index = 0;
  if (!strcmp(argv[1], "DEBUG")) {
    _DEBUG = true;
    replicates = 1;
  } else
    index = atoi(argv[1]); // used for labelling output files

  int start_index = 2;
  if ((argc % 2) == 0) {
    start_index = 2;
  }
  if ((argc % 2) == 1) {
    start_index = 1;
  }
  for (int i = start_index; i < (argc - 1); i += 2) {

    if (!strcmp(argv[i], "-r")) {
      if (!_DEBUG)
        replicates = atoi(argv[i + 1]);
    } else if (!strcmp(argv[i], "-s"))
      scenariosFile = argv[i + 1];
    else if (!strcmp(argv[i], "-n"))
      popFile = argv[i + 1];
    else if (!strcmp(argv[i], "-p"))
      randParamsfile = argv[i + 1];
    else if (!strcmp(argv[i], "-t"))
      dt = atof(argv[i + 1]);
    else if (!strcmp(argv[i], "-o"))
      opDir = argv[i + 1];
    else if (!strcmp(argv[i], "-g"))
      RandomSeedFile = argv[i + 1];
    else if (!strcmp(argv[i], "-c"))
      CoverageReductionFile = argv[i + 1];
    else if (!strcmp(argv[i], "-e"))
      outputEndgame = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-D"))
      outputEndgameDate = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-m"))
      NTDMC = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-N"))
      outputNTDMCDate = atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "-x"))
      reduceImpViaXml = atoi(argv[i + 1]);
    else {
      std::cout << "Error: unknown command line switch " << argv[i]
                << std::endl;
      return 1;
    }
  }
  // input "-m 0" when running from command line if we don't want to
  // output NTDMC data.
  if (NTDMC == 0) {
    outputNTDMC = false;
  }
  std::cout << "outputNTDMC = " << outputNTDMC << std::endl;
  for (int i = 0; i < argc; i++)
    std::cout << argv[i] << " ";
  std::cout << std::endl << std::endl;

  // validate

  if (scenariosFile.length() == 0) {
    std::cout << "Error: Scenarios file undefined." << std::endl;
    return 1;
  }

  if (popFile.length() == 0) {
    std::cout << "Error: Population size file undefined." << std::endl;
    return 1;
  }
  if (!replicates) {
    replicates = 1000;
    std::cout << "Replicates undefined so using default value of " << replicates
              << std::endl;
  }
  if (randParamsfile.length() == 0) {
    std::cout << "Error: Random parameters file undefined." << std::endl;
    return 1;
  }
  std::cout << std::endl;

  if (opDir.length() == 0)
    opDir = "./";
  else if (opDir.back() != '/')
    opDir = opDir + "/";

  // Read the inputs

  TiXmlDocument scenariosDoc(scenariosFile);
  if (!scenariosDoc.LoadFile()) {
    std::cout << "Error: cannot read file " << scenariosFile << std::endl;
    exit(1);
  }
  TiXmlElement *xmlModel = scenariosDoc.RootElement(); //<Model> element
  if (xmlModel == NULL) {
    std::cout << "Error: Invalid file " << scenariosFile
              << ". Does not contain the <Model> root element" << std::endl;
    exit(1);
  }

  TiXmlElement *xmlParameters = xmlModel->FirstChildElement("ParamList");
  if (xmlParameters == NULL) {
    std::cout << "Error: Cannot find parameter values in file " << scenariosFile
              << std::endl;
    exit(1);
  }

  // Create the Vector, Worm and Host population objects
  Vector vectors(xmlParameters);
  Worm worms(xmlParameters);
  Population hostPopulation(xmlParameters);

  hostPopulation.loadPopulationSize(popFile);

  // Create Scenarios
  TiXmlElement *xmlScenarioList = xmlModel->FirstChildElement("ScenarioList");
  if (xmlScenarioList == NULL) {
    std::cout << "Error: Cannot find scenario list in file " << scenariosFile
              << std::endl;
    exit(1);
  }
  ScenariosList Scenarios;
  Scenarios.createScenarios(xmlScenarioList, opDir);

  // Run
  Model model;
  model.runScenarios(Scenarios, hostPopulation, vectors, worms, replicates, dt,
                     index, outputEndgame, outputEndgameDate, outputNTDMC,
                     outputNTDMCDate, reduceImpViaXml, randParamsfile,
                     RandomSeedFile, CoverageReductionFile, opDir);

  gettimeofday(&tv2, NULL);
  double timesofar = (double)(tv2.tv_usec - tv1.tv_usec) / 1000000.0 +
                     (double)(tv2.tv_sec - tv1.tv_sec);
  std::cout << std::endl
            << "Completed successfully in " << timesofar << " secs."
            << std::endl;

  return 0;
}
#endif // DISABLE_MAIN_METHOD
