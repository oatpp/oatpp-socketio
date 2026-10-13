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

/***************************************************************************
 *
 * Shutdown helper for the demo applications.
 *
 * Both servers used to run `while (keepRunning)` with keepRunning never
 * becoming false, so the only way out was SIGKILL - webApiStop() and the
 * environment teardown were unreachable.
 *
 ***************************************************************************/

#ifndef SIO_APP_StopSignal_hpp
#define SIO_APP_StopSignal_hpp

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

namespace oatpp_sio {
namespace app {

/** set by the SIGINT/SIGTERM handler */
inline std::atomic<bool>& stopFlag() {
  static std::atomic<bool> flag{false};
  return flag;
}

/** install SIGINT/SIGTERM handlers (Ctrl-C, systemd stop, docker stop) */
inline void installStopSignals() {
  const auto handler = [](int) { stopFlag().store(true); };
  std::signal(SIGINT, handler);
  std::signal(SIGTERM, handler);
}

/** wait until a stop signal arrived, polling in small steps */
inline void waitForStop(unsigned int stepMs = 100) {
  while (!stopFlag().load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
  }
}

}  // namespace app
}  // namespace oatpp_sio

#endif /* SIO_APP_StopSignal_hpp */
