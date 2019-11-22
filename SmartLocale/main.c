/*
	SmartLocale
	18/NOV/2019 - 22/NOV/2019 | Andrei Florian
*/

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>

#include "applibs_versions.h"
#include "epoll_timerfd_utilities.h"
#include "mt3620_avnet_dev.h"
#include "deviceTwin.h"
#include "azure_iot_utilities.h"
#include "connection_strings.h"
#include "build_options.h"

#include <applibs/log.h>
#include <applibs/i2c.h>
#include <applibs/gpio.h>
#include <applibs/wificonfig.h>
#include <azureiot/iothub_device_client_ll.h>

#include <applibs/rtc.h>
#include <applibs/networking.h>
#include <time.h>
#include <sys/time.h>

#include "universum_utilities.h";

// this is the intervals of time between data sent to the cloud. Set to 1 business day by default
int runtime = 86400; // runtime in seconds

// Provide local access to variables in other files
extern twin_t twinArray[];
extern int twinArraySize;
extern IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle;
extern int accelTimerFd;

// Support functions.
static void TerminationHandler(int signalNumber);
static int InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);

// File descriptors - initialized to invalid value
int epollFd = -1;
bool versionStringSent = false;

// Define the Json string format
static const char JSONBuffer[] = "{\"%s\":%i, \"%s\":%i, \"%s\":%i, \"%s\":%i, \"%s\":%i, \"%s\":\"%i\", \"%s\":\"%i\", \"%s\":\"%s\"}";

// Termination state
volatile sig_atomic_t terminationRequired = false;

// break beam
bool buttonState = false;
int sensorOpen = 0;
bool newVal = false;
int breakCount = 0;

// incrementing variables
int totalSales = 0;
int presence = 0;
int cat1 = 0;
int cat2 = 0;
int cat3 = 0;

// buttons variables
int buttonAOpen;
int buttonBOpen;
int buttonCOpen;

int buttonAPin = 2;
int buttonBPin = 0;
int buttonCPin = 34;

bool buttonAState = false;
bool buttonBState = false;
bool buttonCState = false;

// pins
int r;
int g;
int b;
int buzzer;

// Signal handler for termination requests. This handler must be async-signal-safe
static void TerminationHandler(int signalNumber)
{
	terminationRequired = true;
}

// Allocates and formats a string message on the heap
static void *SetupHeapMessage(const char *messageFormat, size_t maxLength, ...)
{
	va_list args;
	va_start(args, maxLength);
	char *message =
		malloc(maxLength + 1); // Ensure there is space for the null terminator put by vsnprintf.
	if (message != NULL) {
		vsnprintf(message, maxLength, messageFormat, args);
	}
	va_end(args);
	return message;
}

// Direct Method callback function, called when a Direct Method call is received from the Azure IoT Hub
static int DirectMethodCall(const char *methodName, const char *payload, size_t payloadSize, char **responsePayload, size_t *responsePayloadSize)
{
	Log_Debug("\nDirect Method called %s\n", methodName);

	int result = 404; // HTTP status code.

	if (payloadSize < 32) 
	{
		// Declare a char buffer on the stack where we'll operate on a copy of the payload.
		char directMethodCallContent[payloadSize + 1];

		// Prepare the payload for the response. This is a heap allocated null terminated string.
		// The Azure IoT Hub SDK is responsible of freeing it.
		*responsePayload = NULL;  // Reponse payload content.
		*responsePayloadSize = 0; // Response payload content size.


		// Look for the haltApplication method name.  This direct method does not require any payload, other than
		// a valid Json argument such as {}.

		if (strcmp(methodName, "haltApplication") == 0) 
		{
			// Log that the direct method was called and set the result to reflect success!
			Log_Debug("haltApplication() Direct Method called\n");
			result = 200;

			// Construct the response message.  This response will be displayed in the cloud when calling the direct method
			static const char resetOkResponse[] =
				"{ \"success\" : true, \"message\" : \"Halting Application\" }";
			size_t responseMaxLength = sizeof(resetOkResponse);
			*responsePayload = SetupHeapMessage(resetOkResponse, responseMaxLength);
			if (*responsePayload == NULL) {
				Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
				abort();
			}
			*responsePayloadSize = strlen(*responsePayload);

			// Set the terminitation flag to true.  When in Visual Studio this will simply halt the application.
			// If this application was running with the device in field-prep mode, the application would halt
			// and the OS services would resetart the application.
			terminationRequired = true;
			return result;
		}

		// Check to see if the setSensorPollTime direct method was called
		else if (strcmp(methodName, "setSensorPollTime") == 0) 
		{

			// Log that the direct method was called and set the result to reflect success!
			Log_Debug("setSensorPollTime() Direct Method called\n");
			result = 200;

			// The payload should contain a JSON object such as: {"pollTime": 20}
			if (directMethodCallContent == NULL) 
			{
				Log_Debug("ERROR: Could not allocate buffer for direct method request payload.\n");
				abort();
			}

			// Copy the payload into our local buffer then null terminate it.
			memcpy(directMethodCallContent, payload, payloadSize);
			directMethodCallContent[payloadSize] = 0; // Null terminated string.

			JSON_Value *payloadJson = json_parse_string(directMethodCallContent);

			// Verify we have a valid JSON string from the payload
			if (payloadJson == NULL) 
			{
				goto payloadError;
			}

			// Verify that the payloadJson contains a valid JSON object
			JSON_Object *pollTimeJson = json_value_get_object(payloadJson);
			if (pollTimeJson == NULL) 
			{
				goto payloadError;
			}

			// Pull the Key: value pair from the JSON object, we're looking for {"pollTime": <integer>}
			// Verify that the new timer is < 0
			int newPollTime = (int)json_object_get_number(pollTimeJson, "pollTime");
			if (newPollTime < 1) 
			{
				goto payloadError;
			}
			else 
			{

				Log_Debug("New PollTime %d\n", newPollTime);

				// Construct the response message.  This will be displayed in the cloud when calling the direct method
				static const char newPollTimeResponse[] =
					"{ \"success\" : true, \"message\" : \"New Sensor Poll Time %d seconds\" }";
				size_t responseMaxLength = sizeof(newPollTimeResponse) + strlen(payload);
				*responsePayload = SetupHeapMessage(newPollTimeResponse, responseMaxLength, newPollTime);
				if (*responsePayload == NULL) 
				{
					Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
					abort();
				}
				*responsePayloadSize = strlen(*responsePayload);

				// Define a new timespec variable for the timer and change the timer period
				struct timespec newAccelReadPeriod = { .tv_sec = newPollTime,.tv_nsec = 0 };
				SetTimerFdToPeriod(accelTimerFd, &newAccelReadPeriod);
				return result;
			}
		}
		else 
		{
			result = 404;
			Log_Debug("INFO: Direct Method called \"%s\" not found.\n", methodName);

			static const char noMethodFound[] = "\"method not found '%s'\"";
			size_t responseMaxLength = sizeof(noMethodFound) + strlen(methodName);
			*responsePayload = SetupHeapMessage(noMethodFound, responseMaxLength, methodName);
			if (*responsePayload == NULL) 
			{
				Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
				abort();
			}
			*responsePayloadSize = strlen(*responsePayload);
			return result;
		}

	}
	else 
	{
		Log_Debug("Payload size > 32 bytes, aborting Direct Method execution\n");
		goto payloadError;
	}

	// If there was a payload error, construct the 
	// response message and send it back to the IoT Hub for the user to see
	payloadError:


	result = 400; // Bad request.
	Log_Debug("INFO: Unrecognised direct method payload format.\n");

	static const char noPayloadResponse[] =
		"{ \"success\" : false, \"message\" : \"request does not contain an identifiable "
		"payload\" }";

	size_t responseMaxLength = sizeof(noPayloadResponse) + strlen(payload);
	responseMaxLength = sizeof(noPayloadResponse);
	*responsePayload = SetupHeapMessage(noPayloadResponse, responseMaxLength);
	if (*responsePayload == NULL) 
	{
		Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
		abort();
	}

	*responsePayloadSize = strlen(*responsePayload);

	return result;
}

// turn the RGB LED off
void ledOff()
{
	GPIO_SetValue(b, GPIO_Value_Low);
	GPIO_SetValue(r, GPIO_Value_Low);
	GPIO_SetValue(g, GPIO_Value_Low);
}

// turn the RGB LED red
void ledRed()
{
	GPIO_SetValue(r, GPIO_Value_High);
	GPIO_SetValue(g, GPIO_Value_Low);
	GPIO_SetValue(b, GPIO_Value_Low);
}

// turn the RGB LED blue
void ledBlue()
{
	GPIO_SetValue(b, GPIO_Value_High);
	GPIO_SetValue(g, GPIO_Value_Low);
	GPIO_SetValue(r, GPIO_Value_Low);
}

// turn the RGB LED purple
void ledPurple()
{
	ledOff();

	for (int i = 0; i < 3; i++)
	{
		GPIO_SetValue(b, GPIO_Value_High);
		GPIO_SetValue(g, GPIO_Value_Low);
		GPIO_SetValue(r, GPIO_Value_High);
		delay(1000);
		ledOff();
	}
}

// turn the RGB LED green
void ledGreen()
{
	GPIO_SetValue(b, GPIO_Value_Low);
	GPIO_SetValue(g, GPIO_Value_High);
	GPIO_SetValue(r, GPIO_Value_Low);
}

// turn the RGB LED orange
void ledOrange()
{
	GPIO_SetValue(b, GPIO_Value_Low);
	GPIO_SetValue(g, GPIO_Value_High);
	GPIO_SetValue(r, GPIO_Value_High);
}

// turn the RGB LED white
void ledWhite()
{
	GPIO_SetValue(b, GPIO_Value_High);
	GPIO_SetValue(g, GPIO_Value_High);
	GPIO_SetValue(r, GPIO_Value_Low);
}

// buzz the buzzer
void buzz(float time)
{
	GPIO_SetValue(buzzer, GPIO_Value_High);
	delay(time);
	GPIO_SetValue(buzzer, GPIO_Value_Low);
}

// get the value of button A
int getButtonA()
{
	GPIO_Value_Type newButtonAState;
	int result = GPIO_GetValue(buttonAOpen, &newButtonAState); // read the GPIO value

	if (result < 0) // if read was not allowed
	{
		Log_Debug("  [ERROR]  Access to Pin Denied \n");
		ledPurple();
		return -1;
	}

	return newButtonAState;
}

// get the value of button B
int getButtonB()
{
	GPIO_Value_Type newButtonBState;
	int result = GPIO_GetValue(buttonBOpen, &newButtonBState); // read the GPIO value

	if (result < 0) // if read was not allowed
	{
		Log_Debug("  [ERROR]  Access to Pin Denied \n");
		ledPurple();
		return -1;
	}

	return newButtonBState;
}

// get the value of button C
int getButtonC()
{
	GPIO_Value_Type newButtonCState;
	int result = GPIO_GetValue(buttonCOpen, &newButtonCState); // read the GPIO value

	if (result < 0) // if read was not allowed
	{
		Log_Debug("  [ERROR]  Access to Pin Denied \n");
		ledPurple();
		return -1;
	}

	return newButtonCState;
}

// initialise the buttons
void setupButtons()
{
	Log_Debug("[Setup] Setting Up GPIOs \n");

	Log_Debug("[Setup] Opening Button A \n");
	buttonAOpen = GPIO_OpenAsInput(buttonAPin); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (buttonAOpen < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
		return -1;
	}
	Log_Debug("[Setup] Button A is Open \n");

	Log_Debug("[Setup] Opening Button B \n");
	buttonBOpen = GPIO_OpenAsInput(buttonBPin); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (buttonBOpen < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
		return -1;
	}
	Log_Debug("[Setup] Button B is Open \n");

	Log_Debug("[Setup] Opening Button C \n");
	buttonCOpen = GPIO_OpenAsInput(buttonCPin); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (buttonCOpen < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
		return -1;
	}
	Log_Debug("[Setup] Button C is Open \n");
}

// process the buttons
void processButtons(int a, int b, int c)
{
	bool printStuff = false; // print the status?
	if (a == 1)
	{
		cat1++;
		totalSales++;
		printStuff = true;
		println("[Program] Button A Pressed");
	}

	if (b == 1)
	{
		cat2++;
		totalSales++;
		printStuff = true;
		println("[Program] Button B Pressed");
	}

	if (c == 1) 
	{
		cat3++;
		totalSales++;
		printStuff = true;
		println("[Program] Button C Pressed");
	}

	if (printStuff)
	{
		// print the current number of sales from each category
		Log_Debug("[Info] Button A %i \n", cat1);
		Log_Debug("[Info] Button B %i \n", cat2);
		Log_Debug("[Info] Button C %i \n", cat3);
		Log_Debug("[Info] Total %i \n", totalSales);

		ledGreen(); // turn the LED green
		buzz(1000); // buzz the buzzer for a second
		ledOff();
	}
}

// set connections of RGB LED
void setConnections()
{
	Log_Debug("[Setup] Opening GPIO R \n");
	r = GPIO_OpenAsOutput(1, GPIO_OutputMode_PushPull, GPIO_Value_Low); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (r < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
	}

	Log_Debug("[Setup] GPIO is Open \n");

	Log_Debug("[Setup] Opening GPIO G \n");
	g = GPIO_OpenAsOutput(31, GPIO_OutputMode_PushPull, GPIO_Value_Low); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (g < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
	}

	Log_Debug("[Setup] GPIO is Open \n");

	Log_Debug("[Setup] Opening GPIO B \n");
	b = GPIO_OpenAsOutput(35, GPIO_OutputMode_PushPull, GPIO_Value_Low); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (b < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
	}

	Log_Debug("[Setup] GPIO is Open \n");

	Log_Debug("[Setup] Opening GPIO Buzz\n");
	buzzer = GPIO_OpenAsOutput(43, GPIO_OutputMode_PushPull, GPIO_Value_Low); // open the GPIO pin as an output

	// check if the pin was opened successfully
	if (buzzer < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
	}

	Log_Debug("[Setup] GPIO is Open \n");
}

// Set up SIGTERM termination handler, initialize peripherals, and set up event handlers
static int InitPeripheralsAndHandlers(void)
{
	Log_Debug("[Setup] Setting Up GPIOs \n");

	Log_Debug("[Setup] Opening Sesnor Pin \n");
	sensorOpen = GPIO_OpenAsInput(16); // open the GPIO pin of the break beam sensor as an output

	// check if the pin was opened successfully
	if (sensorOpen < 0)
	{
		Log_Debug("[Setup] GPIO Failed to Open \n");
		ledPurple();
		return -1;
	}

	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	epollFd = CreateEpollFd();
	if (epollFd < 0) 
	{
		ledPurple();
		return -1;
	}

	setupButtons(); // setup the buttons
	setConnections(); // setup the RGB LED
	// Tell the system about the callback function that gets called when we receive a device twin update message from Azure
	AzureIoT_SetDeviceTwinUpdateCallback(&deviceTwinChangedHandler);

	return 0;
}

// Close peripherals and handlers
static void ClosePeripheralsAndHandlers(void)
{
	Log_Debug("Closing file descriptors.\n");
	CloseFdAndPrintError(epollFd, "Epoll");
}

// check if the RTC is synched to the cloud
static void CheckTimeSyncState(void)
{
	bool isTimeSyncEnabled = false;
	int result = Networking_TimeSync_GetEnabled(&isTimeSyncEnabled);
	if (result != 0)
	{
		Log_Debug("ERROR: Networking_TimeSync_GetEnabled failed: %s (%d).\n", strerror(errno),
			errno);
		ledPurple();
		return;
	}

	// If time sync is enabled, NTP can reset the time
	if (isTimeSyncEnabled)
	{
		println("[Program] RTC online synch is enabled");
	}
	else
	{
		println("[Program] RTC online synch is disabled");
	}
}

// check if the beam is broken
void processSensor()
{
	GPIO_Value_Type newButtonAState;
	int result = GPIO_GetValue(sensorOpen, &newButtonAState); // read the GPIO value

	if (result < 0) // if read was not allowed
	{
		Log_Debug("[Program] Access to Pin Denied \n");
		ledPurple();
		return -1;
	}

	if (newButtonAState == 0)
	{
		if (newVal)
		{
			breakCount++;
			newVal = false;
			Log_Debug("[Program] Sensor Count %i\n", breakCount);
		}
	}
	else
	{
		newVal = true;
	}
}

// reset variables after sending data to cloud
void resetVariables()
{
	breakCount = 0;
	totalSales = 0;
	cat1 = 0;
	cat2 = 0;
	cat3 = 0;
	presence = 0;
}

// divide the breaks by 2 to get the presence
void setVariables()
{
	presence = (breakCount / 2);
}

// the main loop of code
int main(int argc, char *argv[])
{
	println("Universum");
	println("    Expanding Boundaries");
	println("");
	println("");

	// Variable to help us send the version string up only once
	println("[Setup] Setting Up Variables");
	bool networkConfigSent = false;
	char ssid[128];
	uint32_t frequency;
	char bssid[20];
	
	// Clear the ssid array
	memset(ssid, 0, 128);
	Log_Debug("	[INFO] Version String: %s\n", argv[1]);

	// stuff for RTC
	struct timeval time_start, time_now;
	bool setVals = true;

	println("[Setup] Setting up RTC");
	CheckTimeSyncState(); // check if the RTC is synched

	if (InitPeripheralsAndHandlers() != 0) // initialise the program
	{
		terminationRequired = true;
	}

	println("[Setup] Setup Complete");
	println("[Program] Starting Main Loop");
	println("");
	println("");
	buzz(1000); // buzz the buzzer
	delay(3000);

	// setting up timer
	gettimeofday(&time_start, NULL);
	time_now = time_start;

	// this is the main loop in the code, it will repeat while the device is functioning correctly
	while (true)
	{
		ledWhite(); // turn the LED white (standby)
		gettimeofday(&time_now, NULL); // get the current time
		if (difftime(time_now.tv_sec, time_start.tv_sec) > runtime) // check if data should be sent to cloud
		{
			println("[Program] Sending Data To Azure");
			char *pjsonBuffer = (char *)malloc(256);
			if (pjsonBuffer == NULL)
			{
				Log_Debug("ERROR: not enough memory to send telemetry");
				ledPurple();
			}

			setVariables(); // divide break beam count by 2 to get presence

			for (int i = 0; i < 3; i++)
			{
				ledOff();
				delay(1000);
				ledWhite();
				delay(1000);
			}

			// setup for getting time
			time_t     now;
			struct tm  ts;
			char       buf[32];

			// Get current time
			time(&now);

			// Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
			ts = *localtime(&now);
			strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.0Z", &ts); // Power Bi standard format

			// snprintf(bufferToSend, 128, JSON Payload Config (defined above), dataLable, dataValue);
			// compiles all data into a buffers
			snprintf(pjsonBuffer, 256, JSONBuffer, "presence", presence, "totalSales", totalSales, "cat1", cat1, "cat2", cat2, "cat3", cat3, "stringpresence", presence, "stringtotalSales", totalSales, "timestamp", buf);
			free(buf); // clear the JSON buffer
			Log_Debug("\n[Info] Sending telemetry %s\n", pjsonBuffer);
			AzureIoT_SendMessage(pjsonBuffer); // send the message to Azure

			// clear the timer and variables
			gettimeofday(&time_start, NULL);
			time_now = time_start;
			resetVariables();
			free(pjsonBuffer); // clear the JSON buffer
		}

		processSensor(); // check if beam is broken
		processButtons(getButtonA(), getButtonB(), getButtonC()); // check if buttons are pressed
		// Setup the IoT Hub client.
		// Notes:
		// - it is safe to call this function even if the client has already been set up, as in
		//   this case it would have no effect;
		// - a failure to setup the client is a fatal error.
		if (!AzureIoT_SetupClient()) 
		{
			Log_Debug("ERROR: Failed to set up IoT Hub client\n");
			break;
		}

		WifiConfig_ConnectedNetwork network;
		int result = WifiConfig_GetCurrentNetwork(&network);

		if (result < 0) 
		{
			Log_Debug("INFO: Not currently connected to a WiFi network.\n");
		}
		else 
		{
			frequency = network.frequencyMHz;
			snprintf(bssid, JSON_BUFFER_SIZE, "%02x:%02x:%02x:%02x:%02x:%02x",
				network.bssid[0], network.bssid[1], network.bssid[2],
				network.bssid[3], network.bssid[4], network.bssid[5]);

			if ((strncmp(ssid, (char*)&network.ssid, network.ssidLength) != 0) || !networkConfigSent) 
			{
				memset(ssid, 0, 128);
				strncpy(ssid, network.ssid, network.ssidLength);
				Log_Debug("SSID: %s\n", ssid);
				Log_Debug("Frequency: %dMHz\n", frequency);
				Log_Debug("bssid: %s\n", bssid);
				networkConfigSent = true;

				// Note that we send up this data to Azure if it changes, but the IoT Central Properties elements only 
				// show the data that was currenet when the device first connected to Azure.
				checkAndUpdateDeviceTwin("ssid", &ssid, TYPE_STRING, false);
				checkAndUpdateDeviceTwin("freq", &frequency, TYPE_INT, false);
				checkAndUpdateDeviceTwin("bssid", &bssid, TYPE_STRING, false);
			}
		}
		if (iothubClientHandle != NULL && !versionStringSent) 
		{
			checkAndUpdateDeviceTwin("versionString", argv[1], TYPE_STRING, false);
			versionStringSent = true;
		}

		// AzureIoT_DoPeriodicTasks() needs to be called frequently in order to keep active
		// the flow of data with the Azure IoT Hub
		AzureIoT_DoPeriodicTasks();
		delay(500);
	}
}
