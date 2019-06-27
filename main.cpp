// ----------------------------------------------------------------------------
// Copyright 2016-2019 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
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
// ----------------------------------------------------------------------------
#ifndef MBED_TEST_MODE
#include "mbed.h"
#include "kv_config.h"
#include "mbed-cloud-client/MbedCloudClient.h" // Required for new MbedCloudClient()
#include "factory_configurator_client.h"       // Required for fcc_* functions and FCC_* defines
#include "m2mresource.h"                       // Required for M2MResource

#include "mbed-trace/mbed_trace.h"             // Required for mbed_trace_*
#include "mbed-trace-helper.h"                 // Required for mbed_trace_mutex_*

// Pointers to the resources that will be created in main_application().
static MbedCloudClient *cloud_client;
static bool cloud_client_registered = false;

static M2MResource* m2m_get_res;
static M2MResource* m2m_put_res;
static M2MResource* m2m_post_res;


void print_client_ids(void)
{
    printf("Account ID: %s\r\n", cloud_client->endpoint_info()->account_id.c_str());
    printf("ID: %s\r\n", cloud_client->endpoint_info()->endpoint_name.c_str());
    printf("Endpoint Name: %s\r\n", cloud_client->endpoint_info()->internal_endpoint_name.c_str());
}

void button_press(void)
{
    m2m_get_res->set_value(m2m_get_res->get_value_int() + 1);
    printf("\nCounter %" PRId64, m2m_get_res->get_value_int());
}

void put_update(const char* /*object_name*/)
{
    printf("PUT update %d\r\n", (int)m2m_put_res->get_value_int());
}

void execute_post(void */*arguments*/)
{
    printf("POST executed\r\n");
}

void client_registered(void)
{
    printf("Client registered: \r\n");
    print_client_ids();
    cloud_client_registered = true;
}

void client_unregistered(void)
{
    printf("Client unregistered\r\n");
    cloud_client_registered = false;
}

void client_error(int err)
{
    printf("client_error(%d) -> %s\r\n", err, cloud_client->error_description());
}

void update_progress(uint32_t progress, uint32_t total)
{
    uint8_t percent = (uint8_t)((uint64_t)progress * 100 / total);
    printf("Update progress = %" PRIu8 "%%\r\n", percent);
}

void update_authorize(int32_t request)
{
    switch (request) {
        case MbedCloudClient::UpdateRequestDownload:
            printf("Download authorized\r\n");
            cloud_client->update_authorize(MbedCloudClient::UpdateRequestDownload);
            break;

        case MbedCloudClient::UpdateRequestInstall:
            printf("Update authorized -> reboot\r\n");
            cloud_client->update_authorize(MbedCloudClient::UpdateRequestInstall);
            break;

        default:
            printf("update_authorize(%" PRId32 "), unknown request", request);
            break;
    }
}

int main(void)
{
    int status;

    status = mbed_trace_init();
    if (status != 0) {
        printf("mbed_trace_init() failed with %d\r\n", status);
        return -1;
    }

    mbed_trace_config_set(TRACE_ACTIVE_LEVEL_INFO); // trace level info

    printf("Init KVStore\r\n");
    // Mount default kvstore
    status = kv_init_storage_config();
    if (status != MBED_SUCCESS) {
        printf("kv_init_storage_config() - failed, status %d\r\n", status);
        return -1;
    }

    // Connect with NetworkInterface
    NetworkInterface *network = NetworkInterface::get_default_instance();
    if (network == NULL) {
        printf("Failed to get default NetworkInterface\n");
        return -1;
    }
    status = network->connect();
    if (status != NSAPI_ERROR_OK) {
        printf("NetworkInterface failed to connect with %d\n", status);
        return -1;
    }
    printf("Network connected\r\n");

    // Run developer flow
    printf("Using developer flow\r\n");
    status = fcc_init();
    status = fcc_developer_flow();
    if (status != FCC_STATUS_SUCCESS && status != FCC_STATUS_KCM_FILE_EXIST_ERROR) {
        printf("fcc_developer_flow() failed with %d\r\n", status);
        return -1;
    }

    printf("Create resources\r\n");
    M2MObjectList m2m_obj_list;

    // GET resource 3200/0/5501
    m2m_get_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3200, 0, 5501, M2MResourceInstance::INTEGER, M2MBase::GET_ALLOWED);
    if (m2m_get_res->set_value(0) != true) {
        printf("m2m_get_res->set_value() failed\r\n");
        return -1;
    }

    // PUT resource 3200/0/5500
    m2m_put_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3342, 0, 5500, M2MResourceInstance::INTEGER, M2MBase::GET_PUT_ALLOWED);
    if (m2m_put_res->set_value(0) != true) {
        printf("m2m_led_res->set_value() failed\r\n");
        return -1;
    }
    if (m2m_put_res->set_value_updated_function(put_update) != true) { // PUT sets led
        printf("m2m_put_res->set_value_updated_function() failed\r\n");
        return -1;
    }

    m2m_post_res = M2MInterfaceFactory::create_resource(m2m_obj_list, 3342, 0, 5500, M2MResourceInstance::INTEGER, M2MBase::POST_ALLOWED);
    if (m2m_post_res->set_execute_function(execute_post) != true) { // POST toggles led
        printf("m2m_post_res->set_execute_function() failed\r\n");
        return -1;
    }

    printf("Starting Pelion Device Management Client\r\n");
    cloud_client = new MbedCloudClient(client_registered, client_unregistered, client_error, update_authorize, update_progress);
    cloud_client->add_objects(m2m_obj_list);
    cloud_client->setup(network); // cloud_client->setup(NULL); -- https://jira.arm.com/browse/IOTCLT-3114

    printf("Application loop\r\n");
    while(1) {
        int in_char = getchar();
        if (in_char == 'i') {
            print_client_ids(); // When 'i' is pressed, print endpoint info
            continue;
        } else if (in_char > 0 && in_char != 0x03) { // Ctrl+C is 0x03 in Mbed OS and Linux returns negative number
            button_press(); // Simulate button press
            continue;
        }
        printf("Unregistering\r\n");
        cloud_client->close();
        while (cloud_client_registered == true) {
            wait(1);
        }
        break;
    }
    return 0;
}

#endif /* MBED_TEST_MODE */
