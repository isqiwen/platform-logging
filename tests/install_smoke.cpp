#include <platform_logging/logging.h>

int main() {
  platform_logging::Configuration configuration;
  configuration.file.enabled = false;

  std::string error_message;
  if (!platform_logging::Configure(configuration, &error_message)) {
    return 1;
  }

  PLATFORM_LOG_INFO("install smoke ok");
  platform_logging::Shutdown();
  return 0;
}
