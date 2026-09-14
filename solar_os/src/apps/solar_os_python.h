#pragma once

#include "solar_os.h"
#include "solar_os_script_runner.h"

extern const solar_os_app_t solar_os_python_app;

esp_err_t solar_os_python_run(const solar_os_script_run_request_t *request,
                              solar_os_script_run_result_t *result);
