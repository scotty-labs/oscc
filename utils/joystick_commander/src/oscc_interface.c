/**
* @file oscc_interface.c
* @brief OSCC interface source- The main command* functions and
*        the update function should be called on at least a
*        50ms period.  The expectation is that if there is not
*        some kind of communication from the controller to the
*        OSCC modules in that time, then the OSCC modules will
*        disable and return control back to the driver.
*/

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#include "brake_can_protocol.h"
#include "chassis_state_can_protocol.h"
#include "macros.h"
#include "oscc_interface.h"
#include "steering_can_protocol.h"
#include "throttle_can_protocol.h"
#include "canbus.h"

// *****************************************************
// static global types/macros
// *****************************************************

/**
* @brief OSCC interface data - container for the various CAN
*        messages that are used to control the brakes, steering
*        and throttle.  In addition, there are additional
*        variables to store the CAN parameters, handle and
*        channel.
*
*        The entire structure is packed at the single byte
*        level because of the need to send it on the wire to
*        a receiver that is expecting a specific layout.
*/

#pragma pack(push)
#pragma pack(1)

typedef struct {
    oscc_command_brake_data_s brake_cmd;
    oscc_command_throttle_data_s throttle_cmd;
    oscc_command_steering_data_s steering_cmd;
    CanBusT canbus;
    int can_channel;
} oscc_interface_data_s;

typedef struct {
    oscc_report_chassis_state_1_s chassis_state_1;
    oscc_report_chassis_state_2_s chassis_state_2;
    oscc_report_chassis_state_3_s chassis_state_3;
    CanBusT canbus;
    int can_channel;
} oscc_interface_status_data_s;

// restore alignment
#pragma pack(pop)

// *****************************************************
// static global data
// *****************************************************

static oscc_interface_data_s oscc_interface_data;
static oscc_interface_data_s* oscc = NULL;
static oscc_interface_status_data_s oscc_interface_status_data;
static oscc_interface_status_data_s* oscc_status = NULL;

// *****************************************************
// static definitions
// *****************************************************

// *****************************************************
// Function:    oscc_can_write
//
// Purpose:     Wrapper around the canWrite routine from the CAN library
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  id - ID of the CAN message ot send
//              msg - pointer to the buffer to send
//              dlc - size of the buffer
//
// *****************************************************
static int oscc_can_write(long id, void* msg, unsigned int dlc) {
    int return_code = ERROR;
    
    if (oscc != NULL) {
        int ret = can_write(&(oscc->canbus), id, msg, dlc);
        if (ret != -1) {
            return_code = NOERR;
        }
    }
    return return_code;
}

// *****************************************************
// Function:    oscc_interface_init_can
//
// Purpose:     Initialize the OSCC communication layer with known values
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  channel - for now, the CAN channel to use when interacting
//              with the OSCC modules
//
// *****************************************************

int oscc_init_can_impl(int channel, bool virtual) {
    int return_code = ERROR;
    char can_name[255];
    sprintf(can_name, (virtual ? "vcan%d" : "can%d"), channel);
    int ret = can_open(&(oscc_interface_data.canbus), can_name);
    if(ret == 0){
        return_code == NOERR;
    }
    return return_code;
}

int oscc_init_can(int channel) {
    return oscc_init_can_impl(channel, false);
}

int oscc_init_virtual_can(int channel) {
    return oscc_init_can_impl(channel, true);
}

// *****************************************************
// Function:    oscc_interface_check_for_operator_override
//
// Purpose:     Checks report messages for override flag.
//
// Returns:     bool - override occurred flag
//
// Parameters:  can_id - ID of CAN frame containing the report
//              buffer - Buffer of CAN frame containing the report
//
// *****************************************************
static bool oscc_interface_check_for_operator_override(oscc_status_s* status,
    long can_id,
    unsigned char* buffer) {
        bool ret = false;
        if (can_id == OSCC_REPORT_BRAKE_CAN_ID) {
            oscc_report_brake_data_s* brake_report_data =
            (oscc_report_brake_data_s*)buffer;
            
            status->operator_override = (bool)brake_report_data->override;
            status->brake_override = (bool)brake_report_data->override;
            ret = true;
        } else if (can_id == OSCC_REPORT_THROTTLE_CAN_ID) {
            oscc_report_throttle_data_s* throttle_report_data =
            (oscc_report_throttle_data_s*)buffer;
            
            status->operator_override = (bool)throttle_report_data->override;
            status->throttle_override = (bool)throttle_report_data->override;
            ret = true;
        } else if (can_id == OSCC_REPORT_STEERING_CAN_ID) {
            oscc_report_steering_data_s* steering_report_data =
            (oscc_report_steering_data_s*)buffer;
            
            status->operator_override = (bool)steering_report_data->override;
            status->steering_override = (bool)steering_report_data->override;
            ret = true;
        }
        
        return ret;
}
    
// *****************************************************
// Function:    oscc_interface_check_for_obd_timeouts
//
// Purpose:     Checks report messages for OBD timeout flag.
//
// Returns:     bool - timeout occurred flag
//
// Parameters:  can_id - ID of CAN frame containing the report
//              buffer - Buffer of CAN frame containing the report
//
// *****************************************************
static void oscc_interface_check_for_obd_timeout(oscc_status_s* status,
    long can_id,
    unsigned char* buffer) {
    if (can_id == OSCC_REPORT_BRAKE_CAN_ID) {
        oscc_report_brake_data_s* brake_report_data =
        (oscc_report_brake_data_s*)buffer;
        
        status->obd_timeout_brake = (bool)brake_report_data->fault_obd_timeout;
    } else if (can_id == OSCC_REPORT_STEERING_CAN_ID) {
        oscc_report_steering_data_s* steering_report_data =
        (oscc_report_steering_data_s*)buffer;
        
        status->obd_timeout_steering =
        (bool)steering_report_data->fault_obd_timeout;
    }
}

void print_chassis_state_1(
    oscc_report_chassis_state_1_data_s* chassis_state_1_data) {
    printf("left: %u, right: %u, brk_lts: %u, st_angle: %d, brk_p: %d\n",
    chassis_state_1_data->flags &
    OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_LEFT_TURN_SIGNAL_ON,
    chassis_state_1_data->flags &
    OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_RIGHT_TURN_SIGNAL_ON,
    chassis_state_1_data->flags &
    OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_BRAKE_SIGNAL_ON,
    chassis_state_1_data->steering_wheel_angle,
    chassis_state_1_data->brake_pressure);
}
            
void print_chassis_state_2(
    oscc_report_chassis_state_2_data_s* chassis_state_2_data) {
    printf("FL: %d, FR: %d, RL: %d, RR: %d\n",
    chassis_state_2_data->wheel_speed_front_left,
    chassis_state_2_data->wheel_speed_front_right,
    chassis_state_2_data->wheel_speed_rear_left,
    chassis_state_2_data->wheel_speed_rear_right);
}
                
void print_chassis_state_3(
    oscc_report_chassis_state_3_data_s* chassis_state_3_data) {
    printf("RPM: %u, Temp: %u, Gear: %u, Speed: %u, Acc: %u\n",
    chassis_state_3_data->engine_rpm, chassis_state_3_data->engine_temp,
    chassis_state_3_data->gear_position,
    chassis_state_3_data->vehicle_speed,
    chassis_state_3_data->accelerator_pedal_position);
}
                    
void fill_vehicle_status_fields_from_chassis_messages(
    oscc_vehicle_status_s* vehicle_status,
    oscc_report_chassis_state_1_data_s* chassis_state_1_data,
    oscc_report_chassis_state_2_data_s* chassis_state_2_data,
    oscc_report_chassis_state_3_data_s* chassis_state_3_data) {
    if (chassis_state_1_data != NULL) {
        vehicle_status->left_turn_signal =
        chassis_state_1_data->flags &
        OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_LEFT_TURN_SIGNAL_ON;
        vehicle_status->right_turn_signal =
        chassis_state_1_data->flags &
        OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_RIGHT_TURN_SIGNAL_ON;
        vehicle_status->brake_lights =
        chassis_state_1_data->flags &
        OSCC_REPORT_CHASSIS_STATE_1_FLAGS_BIT_BRAKE_SIGNAL_ON;
        vehicle_status->steering_wheel_angle =
        chassis_state_1_data->steering_wheel_angle;
        vehicle_status->brake_pressure = chassis_state_1_data->brake_pressure;
    }
    
    if (chassis_state_2_data != NULL) {
        vehicle_status->wheel_speed_front_left =
        chassis_state_2_data->wheel_speed_front_left;
        vehicle_status->wheel_speed_front_right =
        chassis_state_2_data->wheel_speed_front_right;
        vehicle_status->wheel_speed_rear_left =
        chassis_state_2_data->wheel_speed_rear_left;
        vehicle_status->wheel_speed_rear_right =
        chassis_state_2_data->wheel_speed_rear_right;
    }
    
    if (chassis_state_3_data != NULL) {
        vehicle_status->engine_rpm = chassis_state_3_data->engine_rpm;
        vehicle_status->engine_temperature = chassis_state_3_data->engine_temp;
        vehicle_status->gear_position = chassis_state_3_data->gear_position;
        vehicle_status->vehicle_speed = chassis_state_3_data->vehicle_speed;
        vehicle_status->accelerator_pedal_position =
        chassis_state_3_data->accelerator_pedal_position;
    }
}
                        
static void oscc_interface_parse_vehicle_state_info(
    oscc_vehicle_status_s* vehicle_status, long can_id, unsigned int msg_dlc,
    unsigned int msg_flag, unsigned int tstamp, unsigned char* buffer) {
    if (can_id == OSCC_REPORT_CHASSIS_STATE_1_CAN_ID) {
        oscc_report_chassis_state_1_data_s* chassis_state_1_data = 
        (oscc_report_chassis_state_1_data_s*)buffer;

    oscc_status->chassis_state_1.id = (uint32_t)can_id;
    oscc_status->chassis_state_1.dlc = (uint8_t)msg_dlc;
    oscc_status->chassis_state_1.timestamp = (uint32_t)tstamp;

    memcpy(&oscc_status->chassis_state_1.data, chassis_state_1_data,
        sizeof(oscc_report_chassis_state_1_data_s));

    fill_vehicle_status_fields_from_chassis_messages(
        vehicle_status, chassis_state_1_data, NULL, NULL);

    } else if (can_id == OSCC_REPORT_CHASSIS_STATE_2_CAN_ID) {
        oscc_report_chassis_state_2_data_s* chassis_state_2_data =
            (oscc_report_chassis_state_2_data_s*)buffer;

    oscc_status->chassis_state_2.id = (uint32_t)can_id;
    oscc_status->chassis_state_2.dlc = (uint8_t)msg_dlc;
    oscc_status->chassis_state_2.timestamp = (uint32_t)tstamp;

    memcpy(&oscc_status->chassis_state_2.data, chassis_state_2_data,
        sizeof(oscc_report_chassis_state_2_data_s));

    fill_vehicle_status_fields_from_chassis_messages(
        vehicle_status, NULL, chassis_state_2_data, NULL);

    } else if (can_id == OSCC_REPORT_CHASSIS_STATE_3_CAN_ID) {
        oscc_report_chassis_state_3_data_s* chassis_state_3_data =
            (oscc_report_chassis_state_3_data_s*)buffer;

    oscc_status->chassis_state_3.id = (uint32_t)can_id;
    oscc_status->chassis_state_3.dlc = (uint8_t)msg_dlc;
    oscc_status->chassis_state_3.timestamp = (uint32_t)tstamp;
    memcpy(&oscc_status->chassis_state_3.data, chassis_state_3_data,
        sizeof(oscc_report_chassis_state_3_data_s));
    
    fill_vehicle_status_fields_from_chassis_messages(
        vehicle_status, NULL, NULL, chassis_state_3_data);
    }
}
                                                    
// *****************************************************
// public definitions
// *****************************************************

// *****************************************************
// Function:    oscc_interface_set_defaults
//
// Purpose:     Initialize the OSCC communication layer with known values
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_set_defaults() {
    int return_code = NOERR;

    oscc_interface_data.brake_cmd.enabled = 0;
    oscc_interface_data.brake_cmd.pedal_command = 0;

    oscc_interface_data.throttle_cmd.enabled = 0;
    oscc_interface_data.throttle_cmd.commanded_accelerator_position = 0;

    oscc_interface_data.steering_cmd.enabled = 0;
    oscc_interface_data.steering_cmd.commanded_steering_wheel_angle = 0;
    oscc_interface_data.steering_cmd.commanded_steering_wheel_angle_rate = 0;

    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_init
//
// Purpose:     Initialize the OSCC interface - CAN communication
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  channel - integer value containing the CAN channel to openk
//
// *****************************************************
int oscc_interface_init(int channel) {
    int return_code = ERROR;

    oscc_interface_set_defaults();

    return_code = oscc_init_can(channel);

    if (return_code == NOERR) {
        oscc = &oscc_interface_data;
        oscc_status = &oscc_interface_status_data;
    }

    return (return_code);
}

int oscc_interface_init_no_defaults(int channel) {
    int return_code = oscc_init_can(channel);

    if (return_code == NOERR) {
        oscc = &oscc_interface_data;
        oscc_status = &oscc_interface_status_data;
    }

    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_close
//
// Purpose:     Release resources and close the interface
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
void oscc_interface_close() {
    if (oscc != NULL) {
        can_close(&(oscc->canbus));
    }
    oscc = NULL;
}

// *****************************************************
// Function:    oscc_interface_enable
//
// Purpose:     Cause the initialized interface to enable control of the
//              vehicle using the OSCC modules
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_enable() {
    int return_code = ERROR;

    if (oscc != NULL) {
        return_code = NOERR;

        oscc->brake_cmd.enabled = 1;
        oscc->throttle_cmd.enabled = 1;
        oscc->steering_cmd.enabled = 1;
    }

    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_command_brakes
//
// Purpose:     Send a CAN message to set the brakes to a commanded value
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  brake_setpoint - unsigned value
//              The value is range limited between 0 and 52428
//
// *****************************************************
int oscc_interface_command_brakes(unsigned int brake_setpoint) {
    int return_code = ERROR;

    if (oscc != NULL) {
        oscc->brake_cmd.pedal_command = (uint16_t)brake_setpoint;

        return_code = oscc_can_write(OSCC_COMMAND_BRAKE_CAN_ID, (void*)&oscc->brake_cmd, sizeof(oscc->brake_cmd));
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_command_throttle
//
// Purpose:     Send a CAN message to set the throttle to a commanded value
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  throttle_setpoint - unsigned value
//              The value is range limited between 0 and 19660
//
// *****************************************************
int oscc_interface_command_throttle(unsigned int throttle_setpoint) {
    int return_code = ERROR;

    if (oscc != NULL) {
        oscc->throttle_cmd.commanded_accelerator_position = (uint16_t)throttle_setpoint;

        return_code = oscc_can_write(OSCC_COMMAND_THROTTLE_CAN_ID,
            (void*)&oscc->throttle_cmd,
            sizeof(oscc->throttle_cmd));
    }

    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_command_steering
//
// Purpose:     Send a CAN message to set the steering to a commanded value
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  angle - signed value: the steering angle in degrees
//              rate - unsigned value; the steering rate in degrees/sec
//
//              angle is range limited between -4700 to 4700
//              rate is range limited between 20 to 254
//
// *****************************************************
int oscc_interface_command_steering(int angle, unsigned int rate) {
    int return_code = ERROR;

    if (oscc != NULL) {
        oscc->steering_cmd.commanded_steering_wheel_angle = (int16_t)angle;
        oscc->steering_cmd.commanded_steering_wheel_angle_rate = (uint16_t)rate;
        
        return_code = oscc_can_write(OSCC_COMMAND_STEERING_CAN_ID,
            (void*)&oscc->steering_cmd,
            sizeof(oscc->steering_cmd));
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_disable_brakes
//
// Purpose:     Send a specific CAN message to set the brake enable value
//              to 0.  Included with this is a safe brake setting
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_disable_brakes() {
    int return_code = ERROR;
    
    if (oscc != NULL) {
        oscc->brake_cmd.enabled = 0;
        
        printf("brake: %d %d\n", oscc->brake_cmd.enabled,
        oscc->brake_cmd.pedal_command);
        
        return_code = oscc_interface_command_brakes(0);
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_disable_throttle
//
// Purpose:     Send a specific CAN message to set the throttle enable value
//              to 0.  Included with this is a safe throttle setting
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_disable_throttle() {
    int return_code = ERROR;
    
    if (oscc != NULL) {
        oscc->throttle_cmd.enabled = 0;
        
        printf("throttle: %d %d\n", oscc->throttle_cmd.enabled,
        oscc->throttle_cmd.commanded_accelerator_position);
        
        return_code = oscc_interface_command_throttle(0);
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_disable_steering
//
// Purpose:     Send a specific CAN message to set the steering enable value
//              to 0.  Included with this is a safe steering angle and rate
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_disable_steering() {
    int return_code = ERROR;
    
    if (oscc != NULL) {
        oscc->steering_cmd.enabled = 0;
        
        printf("steering: %d %d %d\n", oscc->steering_cmd.enabled,
        oscc->steering_cmd.commanded_steering_wheel_angle,
        oscc->steering_cmd.commanded_steering_wheel_angle_rate);
        
        return_code = oscc_interface_command_steering(0, 0);
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_disable
//
// Purpose:     Send a series of CAN messages to disable all of the OSCC
//              modules.  Mostly a wrapper around the existing specific
//              disable functions
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  void
//
// *****************************************************
int oscc_interface_disable() {
    int return_code = oscc_interface_disable_brakes();
    
    if (return_code == NOERR) {
        return_code = oscc_interface_disable_throttle();
        
        if (return_code == NOERR) {
            return_code = oscc_interface_disable_steering();
        }
    }
    return (return_code);
}

// *****************************************************
// Function:    oscc_interface_update_status
//
// Purpose:     Read CAN messages from the OSCC modules and check for  status
//              changes.
//
// Returns:     int - ERROR or NOERR
//
// Parameters:  override - pointer to an integer value that is filled out if
//              the OSCC modules indicate any override status
//
// *****************************************************
int oscc_interface_update_status(oscc_status_s* status) {
    int return_code = ERROR;
    
    if (oscc != NULL) {
        CanFrame res = can_read(&(oscc->canbus));
        if (res.err_no == 0) {
            return_code = NOERR;
            oscc_interface_check_for_operator_override(status, res.id , res.data);
            oscc_interface_check_for_obd_timeout(status, res.id, res.data);
        } 
        else{
            return_code = ERROR;
        }
    }
    return return_code;
}
    
int oscc_interface_read_vehicle_status_from_bus(
    oscc_vehicle_status_s* vehicle_status) {
    int return_code = ERROR;
    if (oscc != NULL) {
        unsigned int msg_flag = 0;
        unsigned long tstamp = 0;
        CanFrame res = can_read(&(oscc->canbus));
        if (res.err_no == 0) {
            return_code = NOERR;
            oscc_interface_parse_vehicle_state_info(
                vehicle_status, res.id, res.length, msg_flag, tstamp, res.data);
        }
        else{
            return_code = ERROR;
        }
    }
    return return_code;
}
                
int oscc_interface_read_vehicle_status_from_mem(
    oscc_vehicle_status_s* vehicle_status) {
    int return_code = ERROR;
    if (oscc_status != NULL) {
        fill_vehicle_status_fields_from_chassis_messages(
            vehicle_status, &oscc_status->chassis_state_1.data,
            &oscc_status->chassis_state_2.data,
            &oscc_status->chassis_state_3.data);
    }
        
    return return_code;
}
