#ifndef DEMO_TASK_H
#define DEMO_TASK_H

void DemoTask_Init(void);
void MyDemoTask(void);

#define DEMO_TYPE_FIRMWARE_VERSION    0x01U
#define DEMO_TYPE_TX_POWER            0x02U
#define DEMO_TYPE_LINK_READY          0x03U
#define DEMO_TYPE_START_LINK          0x20U
#define DEMO_TYPE_STOP_LINK           0x21U
#define DEMO_TYPE_PING                0x22U
#define DEMO_TYPE_UNKNOWN             0x7FU

#define DEMO_FW_VERSION_DEFAULT       1
#define DEMO_FW_VERSION_SET           99
#define DEMO_TX_POWER_DEFAULT         10
#define DEMO_TX_POWER_MIN             1
#define DEMO_TX_POWER_MAX             20
#define DEMO_TX_POWER_SET             15
#define DEMO_TX_POWER_INVALID         99
#define DEMO_PING_NONCE               42

#define DEMO_STEP_COUNT               12U
#define DEMO_REQUEST_CONTROL          0xFFU

#endif /* DEMO_TASK_H */
