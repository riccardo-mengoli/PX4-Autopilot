/****************************************************************************
 *
 *   Copyright (c) 2021 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include "SensorAgpSim.hpp"

#include <drivers/drv_sensor.h>
#include <lib/drivers/device/Device.hpp>
#include <lib/geo/geo.h>

using namespace matrix;

SensorAgpSim::SensorAgpSim() :
	ModuleParams(nullptr),
	ScheduledWorkItem(MODULE_NAME, px4::wq_configurations::hp_default)
{
}

SensorAgpSim::~SensorAgpSim()
{
	perf_free(_loop_perf);
}

bool SensorAgpSim::init()
{
	ScheduleOnInterval(500_ms); // 2 Hz
	return true;
}

float SensorAgpSim::generate_wgn()
{
	// generate white Gaussian noise sample with std=1
	// from BlockRandGauss.hpp
	static float V1, V2, S;
	static bool phase = true;
	float X;

	if (phase) {
		do {
			float U1 = (float)rand() / (float)RAND_MAX;
			float U2 = (float)rand() / (float)RAND_MAX;
			V1 = 2.0f * U1 - 1.0f;
			V2 = 2.0f * U2 - 1.0f;
			S = V1 * V1 + V2 * V2;
		} while (S >= 1.0f || fabsf(S) < 1e-8f);

		X = V1 * float(sqrtf(-2.0f * float(logf(S)) / S));

	} else {
		X = V2 * float(sqrtf(-2.0f * float(logf(S)) / S));
	}

	phase = !phase;
	return X;
}

void SensorAgpSim::Run()
{
	if (should_exit()) {
		ScheduleClear();
		exit_and_cleanup();
		return;
	}

	perf_begin(_loop_perf);

	// Check if parameters have changed
	if (_parameter_update_sub.updated()) {
		// clear update
		parameter_update_s param_update;
		_parameter_update_sub.copy(&param_update);

		updateParams();
	}

	if (_vehicle_global_position_sub.updated()) {

		vehicle_global_position_s gpos{};
		_vehicle_global_position_sub.copy(&gpos);

		double latitude = gpos.lat + math::degrees((double)generate_wgn() * 0.2 / CONSTANTS_RADIUS_OF_EARTH);
		double longitude = gpos.lon + math::degrees((double)generate_wgn() * 0.2 / CONSTANTS_RADIUS_OF_EARTH);
		double altitude = (double)(gpos.alt + (generate_wgn() * 0.5f));

		vehicle_global_position_s sample{};

		sample.timestamp_sample = gpos.timestamp_sample;
		sample.lat = latitude;
		sample.lon = longitude;
		sample.alt = altitude;
		sample.lat_lon_valid = true;
		sample.alt_ellipsoid = altitude;
		sample.alt_valid = true;
		sample.eph = 0.9f;
		sample.epv = 1.78f;

		sample.timestamp = hrt_absolute_time();
		_aux_global_position_pub.publish(sample);
	}

	perf_end(_loop_perf);
}

int SensorAgpSim::task_spawn(int argc, char *argv[])
{
	SensorAgpSim *instance = new SensorAgpSim();

	if (instance) {
		_object.store(instance);
		_task_id = task_id_is_work_queue;

		if (instance->init()) {
			return PX4_OK;
		}

	} else {
		PX4_ERR("alloc failed");
	}

	delete instance;
	_object.store(nullptr);
	_task_id = -1;

	return PX4_ERROR;
}

int SensorAgpSim::custom_command(int argc, char *argv[])
{
	return print_usage("unknown command");
}

int SensorAgpSim::print_usage(const char *reason)
{
	if (reason) {
		PX4_WARN("%s\n", reason);
	}

	PRINT_MODULE_DESCRIPTION(
		R"DESCR_STR(
### Description


)DESCR_STR");

	PRINT_MODULE_USAGE_NAME("sensor_agp_sim", "system");
	PRINT_MODULE_USAGE_COMMAND("start");
	PRINT_MODULE_USAGE_DEFAULT_COMMANDS();

	return 0;
}

extern "C" __EXPORT int sensor_agp_sim_main(int argc, char *argv[])
{
	return SensorAgpSim::main(argc, argv);
}
