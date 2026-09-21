/*
 * PARSECS High Level demo: motherboard commissions a comm board through
 * Get / Set / Call. Layer 1 I2C DMA, L2/L3, and L4/L6/L7 run in
 * PARSECS_LowLevelTask and PARSECS_Protocol_Interface_Task. This file must
 * not call TRANSMIT_APP / RECEIVE_APP. The two Application copies stay
 * identical; -DSPI_MASTER picks the sequencer vs the comm-board object.
 */

#include "demo_task.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "main.h"
#include "usbd_cdc_if.h"
#include "PARSECS_Protocol_Interface.h"
#include <ber.h>

#define LED_HEARTBEAT_Pin   LD4_Pin
#define LED_WARNING_Pin     LD3_Pin
#define LED_FAULT_Pin       LD5_Pin

#ifdef SPI_MASTER
#define ROLE_TAG            "[Master]"
#define DEMO_SELF           CORE_TX_WIT_MOTHERBOARD
#define DEMO_PEER           CORE_TX_WIT_COMM_BOARD
#else
#define ROLE_TAG            "[Slave] "
#define DEMO_SELF           CORE_TX_WIT_COMM_BOARD
#define DEMO_PEER           CORE_TX_WIT_MOTHERBOARD
#endif

#define DEMO_PAYLOAD_NULL   0U
#define DEMO_PAYLOAD_INT    1U

static uint8_t ber_tx[32];
static uint8_t ber_rx[64];

static void usb_print(const char *msg)
{
	if (CDC_Transmit_FS((uint8_t *)msg, (uint16_t)strlen(msg)) == USBD_BUSY)
	{
		HAL_GPIO_TogglePin(GPIOD, LED_WARNING_Pin);
	}
}

static const char *type_name(uint8_t type)
{
	switch (type)
	{
		case DEMO_TYPE_FIRMWARE_VERSION:
			return "FirmwareVersion";
		case DEMO_TYPE_TX_POWER:
			return "TxPower";
		case DEMO_TYPE_LINK_READY:
			return "LinkReady";
		case DEMO_TYPE_START_LINK:
			return "StartLink";
		case DEMO_TYPE_STOP_LINK:
			return "StopLink";
		case DEMO_TYPE_PING:
			return "Ping";
		case DEMO_TYPE_UNKNOWN:
			return "Unknown";
		default:
			return "Type";
	}
}

static const char *op_name(PARSECS_APP_OperationType op)
{
	switch (op)
	{
		case PARSECS_APP_GetRequest:
			return "GetRequest";
		case PARSECS_APP_GetResponse:
			return "GetResponse";
		case PARSECS_APP_SetRequest:
			return "SetRequest";
		case PARSECS_APP_SetResponse:
			return "SetResponse";
		case PARSECS_APP_CallRequest:
			return "CallRequest";
		case PARSECS_APP_CallResponse:
			return "CallResponse";
		default:
			return "Op";
	}
}

static const char *ctrl_name(uint8_t ctrl)
{
	switch (ctrl)
	{
		case PARSECS_APP_Success:
			return "Success";
		case PARSECS_APP_ReadDenied:
			return "ReadDenied";
		case PARSECS_APP_WriteDenied:
			return "WriteDenied";
		case PARSECS_APP_ParameterMethodUndefined:
			return "ParameterMethodUndefined";
		case PARSECS_APP_ParameterSyntaxError:
			return "ParameterSyntaxError";
		case PARSECS_APP_OperationUnsupported:
			return "OperationUnsupported";
		default:
			return "Ctrl";
	}
}

static bool encode_payload(uint8_t kind, int8_t value, uint8_t *buf, uint32_t *len)
{
	*len = 0U;
	if (kind == DEMO_PAYLOAD_NULL)
	{
		return BERNullDataEncode(buf, len);
	}
	return BERIntegerEncode(value, buf, len);
}

static bool decode_int(const uint8_t *buf, uint32_t len, int8_t *value)
{
	uint32_t idx = 0U;
	if (BERIntegerDecode(value, buf, &idx, len) == false)
	{
		return false;
	}
	return (idx == len);
}

static bool decode_null(const uint8_t *buf, uint32_t len)
{
	uint32_t idx = 0U;
	if (BERNullDataDecode(NULL, buf, &idx, len) == false)
	{
		return false;
	}
	return (idx == len);
}

static int8_t send_app(PARSECS_APP_OperationType op, uint8_t ctrl, uint8_t type,
                       const uint8_t *ber, uint32_t ber_len)
{
	uint32_t packet_size = 0U;
	return PARSECS_Protocol_USER_Send(DEMO_PEER,
	                                  DEMO_SELF,
	                                  DEMO_PEER,
	                                  op,
	                                  ctrl,
	                                  type,
	                                  ber,
	                                  ber_len,
	                                  &packet_size);
}

static int8_t recv_app(uint8_t *src, uint8_t *dst, PARSECS_APP_OperationType *op,
                       uint8_t *ctrl, uint8_t *type, uint32_t *ber_len)
{
	return PARSECS_Protocol_USER_Receive(DEMO_PEER,
	                                     src,
	                                     dst,
	                                     op,
	                                     ctrl,
	                                     type,
	                                     ber_rx,
	                                     ber_len);
}

static void format_item(char *out, size_t n, uint8_t type, uint8_t kind, int8_t value)
{
	if (kind == DEMO_PAYLOAD_INT)
	{
		snprintf(out, n, "%s=%d", type_name(type), (int)value);
	}
	else
	{
		snprintf(out, n, "%s", type_name(type));
	}
}

#ifdef SPI_MASTER

typedef struct
{
	PARSECS_APP_OperationType request_op;
	uint8_t type_id;
	uint8_t request_kind;
	int8_t request_value;
	PARSECS_APP_OperationType expect_op;
	uint8_t expect_ctrl;
	uint8_t expect_kind;
	int8_t expect_value;
} demo_step_t;

static const demo_step_t k_steps[DEMO_STEP_COUNT] =
{
	{ PARSECS_APP_GetRequest, DEMO_TYPE_FIRMWARE_VERSION, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, DEMO_FW_VERSION_DEFAULT },
	{ PARSECS_APP_GetRequest, DEMO_TYPE_TX_POWER, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, DEMO_TX_POWER_DEFAULT },
	{ PARSECS_APP_SetRequest, DEMO_TYPE_TX_POWER, DEMO_PAYLOAD_INT, DEMO_TX_POWER_SET,
	  PARSECS_APP_SetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_GetRequest, DEMO_TYPE_TX_POWER, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, DEMO_TX_POWER_SET },
	{ PARSECS_APP_SetRequest, DEMO_TYPE_FIRMWARE_VERSION, DEMO_PAYLOAD_INT, DEMO_FW_VERSION_SET,
	  PARSECS_APP_SetResponse, PARSECS_APP_WriteDenied, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_SetRequest, DEMO_TYPE_TX_POWER, DEMO_PAYLOAD_INT, DEMO_TX_POWER_INVALID,
	  PARSECS_APP_SetResponse, PARSECS_APP_ParameterSyntaxError, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_GetRequest, DEMO_TYPE_UNKNOWN, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_ParameterMethodUndefined, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_CallRequest, DEMO_TYPE_START_LINK, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_CallResponse, PARSECS_APP_Success, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_GetRequest, DEMO_TYPE_LINK_READY, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, 1 },
	{ PARSECS_APP_CallRequest, DEMO_TYPE_PING, DEMO_PAYLOAD_INT, DEMO_PING_NONCE,
	  PARSECS_APP_CallResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, DEMO_PING_NONCE },
	{ PARSECS_APP_CallRequest, DEMO_TYPE_STOP_LINK, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_CallResponse, PARSECS_APP_Success, DEMO_PAYLOAD_NULL, 0 },
	{ PARSECS_APP_GetRequest, DEMO_TYPE_LINK_READY, DEMO_PAYLOAD_NULL, 0,
	  PARSECS_APP_GetResponse, PARSECS_APP_Success, DEMO_PAYLOAD_INT, 0 },
};

static uint8_t g_step;
static bool g_waiting;
static bool g_done;

static void master_finish(bool pass)
{
	char line[96];
	snprintf(line, sizeof(line), "%s DEMO RESULT %u/%u %s\n",
	         ROLE_TAG,
	         (unsigned)g_step,
	         (unsigned)DEMO_STEP_COUNT,
	         (pass != false) ? "PASS" : "FAIL");
	usb_print(line);
	if (pass == false)
	{
		HAL_GPIO_WritePin(GPIOD, LED_FAULT_Pin, GPIO_PIN_SET);
	}
	g_done = true;
}

static bool payload_matches(const demo_step_t *step, uint32_t ber_len)
{
	int8_t value = 0;
	if (step->expect_kind == DEMO_PAYLOAD_INT)
	{
		if (decode_int(ber_rx, ber_len, &value) == false)
		{
			return false;
		}
		return (value == step->expect_value);
	}
	return decode_null(ber_rx, ber_len);
}

static void log_pass(const demo_step_t *step)
{
	char item[40];
	char line[128];
	format_item(item, sizeof(item), step->type_id, step->expect_kind, step->expect_value);
	if (step->expect_ctrl == PARSECS_APP_Success)
	{
		snprintf(line, sizeof(line), "%s PASS %s %s\n",
		         ROLE_TAG, op_name(step->expect_op), item);
	}
	else
	{
		snprintf(line, sizeof(line), "%s PASS %s %s %s\n",
		         ROLE_TAG, op_name(step->expect_op), item, ctrl_name(step->expect_ctrl));
	}
	usb_print(line);
}

void DemoTask_Init(void)
{
	g_step = 0U;
	g_waiting = false;
	g_done = false;
}

void MyDemoTask(void)
{
	char item[40];
	char line[128];
	uint32_t ber_len = 0U;
	int8_t status;
	uint8_t src = 0U;
	uint8_t dst = 0U;
	uint8_t ctrl = 0U;
	uint8_t type = 0U;
	PARSECS_APP_OperationType op = PARSECS_APP_GetRequest;
	const demo_step_t *step;

	if (g_done != false)
	{
		return;
	}
	if (g_step >= DEMO_STEP_COUNT)
	{
		master_finish(true);
		return;
	}

	step = &k_steps[g_step];

	if (g_waiting == false)
	{
		if (encode_payload(step->request_kind, step->request_value, ber_tx, &ber_len) == false)
		{
			master_finish(false);
			return;
		}
		status = send_app(step->request_op, (uint8_t)DEMO_REQUEST_CONTROL,
		                  step->type_id, ber_tx, ber_len);
		if (status == PARSECS_PROTOCOL_ERROR_BUSY)
		{
			return;
		}
		if (status != PARSECS_PROTOCOL_OK)
		{
			master_finish(false);
			return;
		}
		format_item(item, sizeof(item), step->type_id, step->request_kind, step->request_value);
		snprintf(line, sizeof(line), "%s queued %s %s\n",
		         ROLE_TAG, op_name(step->request_op), item);
		usb_print(line);
		g_waiting = true;
		return;
	}

	status = recv_app(&src, &dst, &op, &ctrl, &type, &ber_len);
	if (status == PARSECS_PROTOCOL_ERROR_NO_DATA)
	{
		return;
	}
	if ((status != PARSECS_PROTOCOL_OK) ||
	    (src != DEMO_PEER) ||
	    (dst != DEMO_SELF) ||
	    (op != step->expect_op) ||
	    (type != step->type_id) ||
	    (ctrl != step->expect_ctrl) ||
	    (payload_matches(step, ber_len) == false))
	{
		snprintf(line, sizeof(line),
		         "%s FAIL expected %s %s %s, got %s type=0x%02X ctrl=%s\n",
		         ROLE_TAG,
		         op_name(step->expect_op),
		         type_name(step->type_id),
		         ctrl_name(step->expect_ctrl),
		         op_name(op),
		         (unsigned)type,
		         ctrl_name(ctrl));
		usb_print(line);
		master_finish(false);
		return;
	}

	log_pass(step);
	HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
	g_step++;
	g_waiting = false;
	if (g_step >= DEMO_STEP_COUNT)
	{
		master_finish(true);
	}
}

#else

static int8_t g_fw_version;
static int8_t g_tx_power;
static int8_t g_link_ready;
static bool g_pending_tx;
static PARSECS_APP_OperationType g_pending_op;
static uint8_t g_pending_ctrl;
static uint8_t g_pending_type;
static uint8_t g_pending_kind;
static int8_t g_pending_value;
static uint32_t g_pending_ber_len;
static uint8_t g_pending_ber[32];

static bool is_method(uint8_t type)
{
	return ((type == DEMO_TYPE_START_LINK) ||
	        (type == DEMO_TYPE_STOP_LINK) ||
	        (type == DEMO_TYPE_PING));
}

static bool is_parameter(uint8_t type)
{
	return ((type == DEMO_TYPE_FIRMWARE_VERSION) ||
	        (type == DEMO_TYPE_TX_POWER) ||
	        (type == DEMO_TYPE_LINK_READY));
}

static void arm_response(PARSECS_APP_OperationType op, uint8_t ctrl, uint8_t type,
                         uint8_t kind, int8_t value)
{
	g_pending_op = op;
	g_pending_ctrl = ctrl;
	g_pending_type = type;
	g_pending_kind = kind;
	g_pending_value = value;
	g_pending_tx = true;
	if (encode_payload(kind, value, g_pending_ber, &g_pending_ber_len) == false)
	{
		g_pending_tx = false;
	}
}

static void handle_get(uint8_t type)
{
	if (type == DEMO_TYPE_FIRMWARE_VERSION)
	{
		arm_response(PARSECS_APP_GetResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_INT, g_fw_version);
	}
	else if (type == DEMO_TYPE_TX_POWER)
	{
		arm_response(PARSECS_APP_GetResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_INT, g_tx_power);
	}
	else if (type == DEMO_TYPE_LINK_READY)
	{
		arm_response(PARSECS_APP_GetResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_INT, g_link_ready);
	}
	else if (is_method(type) != false)
	{
		arm_response(PARSECS_APP_GetResponse, PARSECS_APP_ReadDenied, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else
	{
		arm_response(PARSECS_APP_GetResponse, PARSECS_APP_ParameterMethodUndefined, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
}

static void handle_set(uint8_t type, uint32_t ber_len)
{
	int8_t value = 0;
	if (type == DEMO_TYPE_TX_POWER)
	{
		if ((decode_int(ber_rx, ber_len, &value) == false) ||
		    (value < DEMO_TX_POWER_MIN) ||
		    (value > DEMO_TX_POWER_MAX))
		{
			arm_response(PARSECS_APP_SetResponse, PARSECS_APP_ParameterSyntaxError, type,
			             DEMO_PAYLOAD_NULL, 0);
			return;
		}
		g_tx_power = value;
		arm_response(PARSECS_APP_SetResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else if ((type == DEMO_TYPE_FIRMWARE_VERSION) ||
	         (type == DEMO_TYPE_LINK_READY) ||
	         (is_method(type) != false))
	{
		arm_response(PARSECS_APP_SetResponse, PARSECS_APP_WriteDenied, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else
	{
		arm_response(PARSECS_APP_SetResponse, PARSECS_APP_ParameterMethodUndefined, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
}

static void handle_call(uint8_t type, uint32_t ber_len)
{
	int8_t value = 0;
	if (type == DEMO_TYPE_START_LINK)
	{
		if (decode_null(ber_rx, ber_len) == false)
		{
			arm_response(PARSECS_APP_CallResponse, PARSECS_APP_ParameterSyntaxError, type,
			             DEMO_PAYLOAD_NULL, 0);
			return;
		}
		g_link_ready = 1;
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else if (type == DEMO_TYPE_STOP_LINK)
	{
		if (decode_null(ber_rx, ber_len) == false)
		{
			arm_response(PARSECS_APP_CallResponse, PARSECS_APP_ParameterSyntaxError, type,
			             DEMO_PAYLOAD_NULL, 0);
			return;
		}
		g_link_ready = 0;
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else if (type == DEMO_TYPE_PING)
	{
		if (decode_int(ber_rx, ber_len, &value) == false)
		{
			arm_response(PARSECS_APP_CallResponse, PARSECS_APP_ParameterSyntaxError, type,
			             DEMO_PAYLOAD_NULL, 0);
			return;
		}
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_Success, type,
		             DEMO_PAYLOAD_INT, value);
	}
	else if (is_parameter(type) != false)
	{
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_OperationUnsupported, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	else
	{
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_ParameterMethodUndefined, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
}

static void flush_pending(void)
{
	char item[40];
	char line[128];
	int8_t status;

	if (g_pending_tx == false)
	{
		return;
	}
	status = send_app(g_pending_op, g_pending_ctrl, g_pending_type,
	                  g_pending_ber, g_pending_ber_len);
	if (status == PARSECS_PROTOCOL_ERROR_BUSY)
	{
		return;
	}
	if (status != PARSECS_PROTOCOL_OK)
	{
		return;
	}
	format_item(item, sizeof(item), g_pending_type, g_pending_kind, g_pending_value);
	if (g_pending_ctrl == PARSECS_APP_Success)
	{
		snprintf(line, sizeof(line), "%s queued %s %s %s\n",
		         ROLE_TAG, op_name(g_pending_op), item, ctrl_name(g_pending_ctrl));
	}
	else
	{
		snprintf(line, sizeof(line), "%s queued %s %s %s\n",
		         ROLE_TAG, op_name(g_pending_op), type_name(g_pending_type),
		         ctrl_name(g_pending_ctrl));
	}
	usb_print(line);
	HAL_GPIO_TogglePin(GPIOD, LED_HEARTBEAT_Pin);
	g_pending_tx = false;
}

void DemoTask_Init(void)
{
	g_fw_version = (int8_t)DEMO_FW_VERSION_DEFAULT;
	g_tx_power = (int8_t)DEMO_TX_POWER_DEFAULT;
	g_link_ready = 0;
	g_pending_tx = false;
}

void MyDemoTask(void)
{
	char line[96];
	uint32_t ber_len = 0U;
	int8_t status;
	uint8_t src = 0U;
	uint8_t dst = 0U;
	uint8_t ctrl = 0U;
	uint8_t type = 0U;
	PARSECS_APP_OperationType op = PARSECS_APP_GetRequest;

	flush_pending();
	if (g_pending_tx != false)
	{
		return;
	}

	status = recv_app(&src, &dst, &op, &ctrl, &type, &ber_len);
	if (status != PARSECS_PROTOCOL_OK)
	{
		return;
	}
	if ((src != DEMO_PEER) || (dst != DEMO_SELF))
	{
		return;
	}
	(void)ctrl;

	snprintf(line, sizeof(line), "%s rx %s %s\n",
	         ROLE_TAG, op_name(op), type_name(type));
	usb_print(line);

	if (op == PARSECS_APP_GetRequest)
	{
		handle_get(type);
	}
	else if (op == PARSECS_APP_SetRequest)
	{
		handle_set(type, ber_len);
	}
	else if (op == PARSECS_APP_CallRequest)
	{
		handle_call(type, ber_len);
	}
	else
	{
		arm_response(PARSECS_APP_CallResponse, PARSECS_APP_OperationUnsupported, type,
		             DEMO_PAYLOAD_NULL, 0);
	}
	flush_pending();
}

#endif
