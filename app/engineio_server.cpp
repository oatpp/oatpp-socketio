/*
   Copyright 2012-2025 Simon Vogl <svogl@voxel.at> VoXel Interaction Design - www.voxel.at

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include <stdlib.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "oatpp_sio/webapi/webApp.hpp"
#include "oatpp_sio/eio/engineIo.hpp"

using namespace std;
using namespace oatpp_sio;
using namespace oatpp_sio::webapi;

/**
 *  main
 */
int main(int argc, const char* argv[])
{
    cout << "starting" << endl;
    std::string confFile = "blah.xml";


    for (int i = 1; i < argc; i++) {
        if (string(argv[i]) == "-c") {
            // parse config file
            cout << "ARGV parsing config " << argv[i + 1] << endl;
            confFile = argv[i + 1];
            i++;
        }
    }

    {
        //may return 0 when not able to detect
        const auto processor_count = std::thread::hardware_concurrency();
        cout << "# cores detected " << processor_count << endl;
    }

    // WEB FRONTEND:
    bool enableSwaggerUi = getenv("WEBAPI") ? getenv("WEBAPI")[0] == '1' : true;
    // register URLs first
    if (enableSwaggerUi) {
        addSwaggerServerUrl("http://192.168.2.133:8000/", "lab camera");
    }
    // init the WebApi object
    webApiInit();

    // additional options
    // setupGlobalState(getGlobalState(), enableSwaggerUi, nullptr);

    // start the web server thread
    webApiStart(getGlobalState());

    // DONE INIT WEB FRONTEND

    // configure the engine.io stack for the test suite:
    oatpp_sio::eio::theEngine->setConfig(25000, 20000, 1000000);
    oatpp_sio::eio::theEngine->testMode = true;

    bool keepRunning = true;
    int delay = 1;
    do {
        const auto start = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(2000ms);
        const auto end = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> elapsed = end - start;

    } while (keepRunning);

    cout << "stopping" << endl;
    webApiStop();
    cout << "done" << endl;

    return 0;
}
