
// Copyright 2023 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "psmove_loader.h"

#if ENABLE_PSMOVE_BINDINGS
#include <fmt/format.h>


#pragma region
// The following boilerplate was generated like so:
#if 0
import re
with open("third_party/psmove/include/psmove.h", "r") as infile :
header = infile.read()

pattern = r"^ADDAPI (.+?)\s+ADDCALL (.+?)\((.*?)\);"
found = re.findall(pattern, header, re.M | re.S)

decl = []
defs = []
load = []
for ret_t, fn_name, args in found :
proc_t = fn_name.upper() + "_PROC"
args = " ".join([a.strip() for a in args.split(" ") if a.strip()])
decl.append(f"DECL_PROC({proc_t}, {fn_name}, {ret_t}, {args})")
defs.append(f"{proc_t} {fn_name};")
load.append(f"\t\t\tLOAD_PROC({proc_t}, {fn_name});")

print("\n".join(sorted(set(defs))))
print("\n".join(sorted(set(load))))
#endif

PSMOVE_CONNECTION_TYPE_PROC psmove_connection_type;
PSMOVE_CONNECT_BY_ID_PROC psmove_connect_by_id;
PSMOVE_CONNECT_PROC psmove_connect;
PSMOVE_COUNT_CONNECTED_PROC psmove_count_connected;
PSMOVE_DISCONNECT_PROC psmove_disconnect;
PSMOVE_DUMP_CALIBRATION_PROC psmove_dump_calibration;
PSMOVE_ENABLE_ORIENTATION_PROC psmove_enable_orientation;
PSMOVE_FREE_MEM_PROC psmove_free_mem;
PSMOVE_GET_ACCELEROMETER_FRAME_PROC psmove_get_accelerometer_frame;
PSMOVE_GET_ACCELEROMETER_PROC psmove_get_accelerometer;
PSMOVE_GET_BATTERY_PROC psmove_get_battery;
PSMOVE_GET_BUTTONS_PROC psmove_get_buttons;
PSMOVE_GET_BUTTON_EVENTS_PROC psmove_get_button_events;
PSMOVE_GET_EXT_DATA_PROC psmove_get_ext_data;
PSMOVE_GET_EXT_DEVICE_INFO_PROC psmove_get_ext_device_info;
PSMOVE_GET_GYROSCOPE_FRAME_PROC psmove_get_gyroscope_frame;
PSMOVE_GET_GYROSCOPE_PROC psmove_get_gyroscope;
PSMOVE_GET_IDENTITY_GRAVITY_CALIBRATION_DIRECTION_PROC psmove_get_identity_gravity_calibration_direction;
PSMOVE_GET_IDENTITY_MAGNETOMETER_CALIBRATION_DIRECTION_PROC psmove_get_identity_magnetometer_calibration_direction;
PSMOVE_GET_MAGNETOMETER_3AXISVECTOR_PROC psmove_get_magnetometer_3axisvector;
PSMOVE_GET_MAGNETOMETER_CALIBRATION_RANGE_PROC psmove_get_magnetometer_calibration_range;
PSMOVE_GET_MAGNETOMETER_PROC psmove_get_magnetometer;
PSMOVE_GET_MAGNETOMETER_VECTOR_PROC psmove_get_magnetometer_vector;
PSMOVE_GET_MODEL_PROC psmove_get_model;
PSMOVE_GET_ORIENTATION_PROC psmove_get_orientation;
PSMOVE_GET_SERIAL_PROC psmove_get_serial;
PSMOVE_GET_TEMPERATURE_IN_CELSIUS_PROC psmove_get_temperature_in_celsius;
PSMOVE_GET_TEMPERATURE_PROC psmove_get_temperature;
PSMOVE_GET_TRANSFORMED_ACCELEROMETER_FRAME_3AXISVECTOR_PROC psmove_get_transformed_accelerometer_frame_3axisvector;
PSMOVE_GET_TRANSFORMED_ACCELEROMETER_FRAME_DIRECTION_PROC psmove_get_transformed_accelerometer_frame_direction;
PSMOVE_GET_TRANSFORMED_GRAVITY_CALIBRATION_DIRECTION_PROC psmove_get_transformed_gravity_calibration_direction;
PSMOVE_GET_TRANSFORMED_GYROSCOPE_FRAME_3AXISVECTOR_PROC psmove_get_transformed_gyroscope_frame_3axisvector;
PSMOVE_GET_TRANSFORMED_MAGNETOMETER_CALIBRATION_DIRECTION_PROC psmove_get_transformed_magnetometer_calibration_direction;
PSMOVE_GET_TRANSFORMED_MAGNETOMETER_DIRECTION_PROC psmove_get_transformed_magnetometer_direction;
PSMOVE_GET_TRIGGER_PROC psmove_get_trigger;
PSMOVE_HAS_CALIBRATION_PROC psmove_has_calibration;
PSMOVE_HAS_ORIENTATION_PROC psmove_has_orientation;
PSMOVE_HOST_PAIR_CUSTOM_MODEL_PROC psmove_host_pair_custom_model;
PSMOVE_HOST_PAIR_CUSTOM_PROC psmove_host_pair_custom;
PSMOVE_INIT_PROC psmove_init;
PSMOVE_IS_EXT_CONNECTED_PROC psmove_is_ext_connected;
PSMOVE_IS_REMOTE_PROC psmove_is_remote;
PSMOVE_PAIR_CUSTOM_PROC psmove_pair_custom;
PSMOVE_PAIR_PROC psmove_pair;
PSMOVE_POLL_PROC psmove_poll;
PSMOVE_REINIT_PROC psmove_reinit;
PSMOVE_RESET_MAGNETOMETER_CALIBRATION_PROC psmove_reset_magnetometer_calibration;
PSMOVE_RESET_ORIENTATION_PROC psmove_reset_orientation;
PSMOVE_SAVE_MAGNETOMETER_CALIBRATION_PROC psmove_save_magnetometer_calibration;
PSMOVE_SEND_EXT_DATA_PROC psmove_send_ext_data;
PSMOVE_SET_CALIBRATION_POSE_PROC psmove_set_calibration_pose;
PSMOVE_SET_CALIBRATION_TRANSFORM_PROC psmove_set_calibration_transform;
PSMOVE_SET_LEDS_PROC psmove_set_leds;
PSMOVE_SET_LED_PWM_FREQUENCY_PROC psmove_set_led_pwm_frequency;
PSMOVE_SET_MAGNETOMETER_CALIBRATION_DIRECTION_PROC psmove_set_magnetometer_calibration_direction;
PSMOVE_SET_ORIENTATION_FUSION_TYPE_PROC psmove_set_orientation_fusion_type;
PSMOVE_SET_RATE_LIMITING_PROC psmove_set_rate_limiting;
PSMOVE_SET_REMOTE_CONFIG_PROC psmove_set_remote_config;
PSMOVE_SET_RUMBLE_PROC psmove_set_rumble;
PSMOVE_SET_SENSOR_DATA_BASIS_PROC psmove_set_sensor_data_basis;
PSMOVE_SET_SENSOR_DATA_TRANSFORM_PROC psmove_set_sensor_data_transform;
PSMOVE_UPDATE_LEDS_PROC psmove_update_leds;
PSMOVE_UTIL_GET_DATA_DIR_PROC psmove_util_get_data_dir;
PSMOVE_UTIL_GET_ENV_INT_PROC psmove_util_get_env_int;
PSMOVE_UTIL_GET_ENV_STRING_PROC psmove_util_get_env_string;
PSMOVE_UTIL_GET_FILE_PATH_PROC psmove_util_get_file_path;
PSMOVE_UTIL_GET_SYSTEM_FILE_PATH_PROC psmove_util_get_system_file_path;
PSMOVE_UTIL_GET_TICKS_PROC psmove_util_get_ticks;
PSMOVE_UTIL_SLEEP_MS_PROC psmove_util_sleep_ms;
#pragma endregion

#if _WIN64
#include <windows.h>

static HMODULE PSMoveRuntime = NULL;
#define LOAD_PROC(PROC_TYPE, NAME) NAME = (PROC_TYPE) GetProcAddress(PSMoveRuntime, #NAME);

#endif


void BootPSMove()
{
#if _WIN64
	Assert(PSMoveRuntime == NULL);
	PSMoveRuntime = LoadLibrary(TEXT("psmoveapi.dll"));
	if (PSMoveRuntime != NULL)
	{
		fmt::print("Setting up PS Move runtime found on system... ");
		LOAD_PROC(PSMOVE_INIT_PROC, psmove_init);

		if (psmove_init(PSMOVE_CURRENT_VERSION))
		{
			LOAD_PROC(PSMOVE_CONNECTION_TYPE_PROC, psmove_connection_type);
			LOAD_PROC(PSMOVE_CONNECT_BY_ID_PROC, psmove_connect_by_id);
			LOAD_PROC(PSMOVE_CONNECT_PROC, psmove_connect);
			LOAD_PROC(PSMOVE_COUNT_CONNECTED_PROC, psmove_count_connected);
			LOAD_PROC(PSMOVE_DISCONNECT_PROC, psmove_disconnect);
			LOAD_PROC(PSMOVE_DUMP_CALIBRATION_PROC, psmove_dump_calibration);
			LOAD_PROC(PSMOVE_ENABLE_ORIENTATION_PROC, psmove_enable_orientation);
			LOAD_PROC(PSMOVE_FREE_MEM_PROC, psmove_free_mem);
			LOAD_PROC(PSMOVE_GET_ACCELEROMETER_FRAME_PROC, psmove_get_accelerometer_frame);
			LOAD_PROC(PSMOVE_GET_ACCELEROMETER_PROC, psmove_get_accelerometer);
			LOAD_PROC(PSMOVE_GET_BATTERY_PROC, psmove_get_battery);
			LOAD_PROC(PSMOVE_GET_BUTTONS_PROC, psmove_get_buttons);
			LOAD_PROC(PSMOVE_GET_BUTTON_EVENTS_PROC, psmove_get_button_events);
			LOAD_PROC(PSMOVE_GET_EXT_DATA_PROC, psmove_get_ext_data);
			LOAD_PROC(PSMOVE_GET_EXT_DEVICE_INFO_PROC, psmove_get_ext_device_info);
			LOAD_PROC(PSMOVE_GET_GYROSCOPE_FRAME_PROC, psmove_get_gyroscope_frame);
			LOAD_PROC(PSMOVE_GET_GYROSCOPE_PROC, psmove_get_gyroscope);
			LOAD_PROC(PSMOVE_GET_IDENTITY_GRAVITY_CALIBRATION_DIRECTION_PROC, psmove_get_identity_gravity_calibration_direction);
			LOAD_PROC(PSMOVE_GET_IDENTITY_MAGNETOMETER_CALIBRATION_DIRECTION_PROC, psmove_get_identity_magnetometer_calibration_direction);
			LOAD_PROC(PSMOVE_GET_MAGNETOMETER_3AXISVECTOR_PROC, psmove_get_magnetometer_3axisvector);
			LOAD_PROC(PSMOVE_GET_MAGNETOMETER_CALIBRATION_RANGE_PROC, psmove_get_magnetometer_calibration_range);
			LOAD_PROC(PSMOVE_GET_MAGNETOMETER_PROC, psmove_get_magnetometer);
			LOAD_PROC(PSMOVE_GET_MAGNETOMETER_VECTOR_PROC, psmove_get_magnetometer_vector);
			LOAD_PROC(PSMOVE_GET_MODEL_PROC, psmove_get_model);
			LOAD_PROC(PSMOVE_GET_ORIENTATION_PROC, psmove_get_orientation);
			LOAD_PROC(PSMOVE_GET_SERIAL_PROC, psmove_get_serial);
			LOAD_PROC(PSMOVE_GET_TEMPERATURE_IN_CELSIUS_PROC, psmove_get_temperature_in_celsius);
			LOAD_PROC(PSMOVE_GET_TEMPERATURE_PROC, psmove_get_temperature);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_ACCELEROMETER_FRAME_3AXISVECTOR_PROC, psmove_get_transformed_accelerometer_frame_3axisvector);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_ACCELEROMETER_FRAME_DIRECTION_PROC, psmove_get_transformed_accelerometer_frame_direction);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_GRAVITY_CALIBRATION_DIRECTION_PROC, psmove_get_transformed_gravity_calibration_direction);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_GYROSCOPE_FRAME_3AXISVECTOR_PROC, psmove_get_transformed_gyroscope_frame_3axisvector);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_MAGNETOMETER_CALIBRATION_DIRECTION_PROC, psmove_get_transformed_magnetometer_calibration_direction);
			LOAD_PROC(PSMOVE_GET_TRANSFORMED_MAGNETOMETER_DIRECTION_PROC, psmove_get_transformed_magnetometer_direction);
			LOAD_PROC(PSMOVE_GET_TRIGGER_PROC, psmove_get_trigger);
			LOAD_PROC(PSMOVE_HAS_CALIBRATION_PROC, psmove_has_calibration);
			LOAD_PROC(PSMOVE_HAS_ORIENTATION_PROC, psmove_has_orientation);
			LOAD_PROC(PSMOVE_HOST_PAIR_CUSTOM_MODEL_PROC, psmove_host_pair_custom_model);
			LOAD_PROC(PSMOVE_HOST_PAIR_CUSTOM_PROC, psmove_host_pair_custom);
			LOAD_PROC(PSMOVE_INIT_PROC, psmove_init);
			LOAD_PROC(PSMOVE_IS_EXT_CONNECTED_PROC, psmove_is_ext_connected);
			LOAD_PROC(PSMOVE_IS_REMOTE_PROC, psmove_is_remote);
			LOAD_PROC(PSMOVE_PAIR_CUSTOM_PROC, psmove_pair_custom);
			LOAD_PROC(PSMOVE_PAIR_PROC, psmove_pair);
			LOAD_PROC(PSMOVE_POLL_PROC, psmove_poll);
			LOAD_PROC(PSMOVE_REINIT_PROC, psmove_reinit);
			LOAD_PROC(PSMOVE_RESET_MAGNETOMETER_CALIBRATION_PROC, psmove_reset_magnetometer_calibration);
			LOAD_PROC(PSMOVE_RESET_ORIENTATION_PROC, psmove_reset_orientation);
			LOAD_PROC(PSMOVE_SAVE_MAGNETOMETER_CALIBRATION_PROC, psmove_save_magnetometer_calibration);
			LOAD_PROC(PSMOVE_SEND_EXT_DATA_PROC, psmove_send_ext_data);
			LOAD_PROC(PSMOVE_SET_CALIBRATION_POSE_PROC, psmove_set_calibration_pose);
			LOAD_PROC(PSMOVE_SET_CALIBRATION_TRANSFORM_PROC, psmove_set_calibration_transform);
			LOAD_PROC(PSMOVE_SET_LEDS_PROC, psmove_set_leds);
			LOAD_PROC(PSMOVE_SET_LED_PWM_FREQUENCY_PROC, psmove_set_led_pwm_frequency);
			LOAD_PROC(PSMOVE_SET_MAGNETOMETER_CALIBRATION_DIRECTION_PROC, psmove_set_magnetometer_calibration_direction);
			LOAD_PROC(PSMOVE_SET_ORIENTATION_FUSION_TYPE_PROC, psmove_set_orientation_fusion_type);
			LOAD_PROC(PSMOVE_SET_RATE_LIMITING_PROC, psmove_set_rate_limiting);
			LOAD_PROC(PSMOVE_SET_REMOTE_CONFIG_PROC, psmove_set_remote_config);
			LOAD_PROC(PSMOVE_SET_RUMBLE_PROC, psmove_set_rumble);
			LOAD_PROC(PSMOVE_SET_SENSOR_DATA_BASIS_PROC, psmove_set_sensor_data_basis);
			LOAD_PROC(PSMOVE_SET_SENSOR_DATA_TRANSFORM_PROC, psmove_set_sensor_data_transform);
			LOAD_PROC(PSMOVE_UPDATE_LEDS_PROC, psmove_update_leds);
			LOAD_PROC(PSMOVE_UTIL_GET_DATA_DIR_PROC, psmove_util_get_data_dir);
			LOAD_PROC(PSMOVE_UTIL_GET_ENV_INT_PROC, psmove_util_get_env_int);
			LOAD_PROC(PSMOVE_UTIL_GET_ENV_STRING_PROC, psmove_util_get_env_string);
			LOAD_PROC(PSMOVE_UTIL_GET_FILE_PATH_PROC, psmove_util_get_file_path);
			LOAD_PROC(PSMOVE_UTIL_GET_SYSTEM_FILE_PATH_PROC, psmove_util_get_system_file_path);
			LOAD_PROC(PSMOVE_UTIL_GET_TICKS_PROC, psmove_util_get_ticks);
			LOAD_PROC(PSMOVE_UTIL_SLEEP_MS_PROC, psmove_util_sleep_ms);
			fmt::print("Done!\n");
		}
		else
		{
			fmt::print("Failed!\n");
			fmt::print("Unable to initialize PS Move runtime found on system.  Tangerine requires verison {}.{}.{}.\n",
				PSMOVEAPI_VERSION_MAJOR,
				PSMOVEAPI_VERSION_MINOR,
				PSMOVEAPI_VERSION_PATCH);
			TeardownPSMove();
		}
	}
#endif // _WIN64
}


#undef LOAD_PROC


void TeardownPSMove()
{
#if _WIN64
	if (PSMoveRuntime)
	{
		FreeLibrary(PSMoveRuntime);
		PSMoveRuntime = NULL;
	}
#endif
}


bool PSMoveAvailable()
{
#if _WIN64
	return PSMoveRuntime != NULL;
#else
	return false;
#endif
}


struct MoveConnectionHandle
{
	PSMove* Handle = nullptr;
	PSMove_Connection_Type Connection = Conn_Unknown;
	PSMove_Model_Type Model;
};


MoveConnection::MoveConnection(int InIndex)
	: Index(InIndex)
	, Handle(psmove_connect_by_id(Index))
	, Connection(psmove_connection_type(Handle))
	, Model(psmove_get_model(Handle))
	, Local(!psmove_is_remote(Handle))
	, Serial(psmove_get_serial(Handle))
{
}


int MoveConnection::Score()
{
	// USB connections don't support button events, and are really only useful for a bluetooth
	// pairing workflow, which can be done outside of the application for now anyway.
	// Local connections are preferred over remote ones for lower latency.
	// Accepting connections from unknown models is unlikely to be productive.
	// Likewise, controllers without a valid calibration can't be oriented.
	if (Connection == Conn_Bluetooth && Model > Model_Unknown && psmove_has_calibration(Handle))
	{
		return Local ? 2 : 1;
	}
	else
	{
		return 0;
	}
}


void MoveConnection::SetColor(glm::vec3 Color)
{
	Color = glm::clamp(Color, glm::vec3(0.0), glm::vec3(1.0)) * glm::vec3(255.0);
	psmove_set_leds(Handle, int(Color.r), int(Color.g), int(Color.b));
}

void MoveConnection::Activate()
{
	psmove_enable_orientation(Handle, PSMove_True);
	psmove_set_orientation_fusion_type(Handle, OrientationFusion_ComplementaryMARG);
}

void MoveConnection::Refresh()
{
	psmove_update_leds(Handle);
	psmove_poll(Handle);
	psmove_get_orientation(Handle, &Orientation.w, &Orientation.x, &Orientation.y, &Orientation.z);
	Orientation.y *= -1;
	Orientation.z *= -1;
}

MoveConnection::~MoveConnection()
{
	psmove_disconnect(Handle);
}



#endif // ENABLE_PSMOVE_BINDINGS
