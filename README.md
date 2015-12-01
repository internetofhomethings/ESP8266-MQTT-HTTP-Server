<h2><strong>ESP8266 Web Server Using SDK API</strong></h2>

This project provides a Web Server Framework using either the Arduino Wifi Library or the EspressIf SDK API.

Setup:

1. Copy the webandmqtt_server folder to your Arduino sketch folder.
2. Copy the UtilityFunctions folder to your Arduino libraries folder.
3. Change the following in the webandmqtt_server sketch to match your network settings:

const char* ssid = "YourWifiSSID";
const char* password = "YourWifiPASSWORD";
const IPAddress ipadd(192,168,0,132);     
const IPAddress ipgat(192,168,0,1); 

define SVRPORT 9701 

4. Server Setting

4.1 To use the standard Arduino Web Server library, which polls for connections, use this define in the sketch:

define SVR_TYPE SVR_HTTP_LIB

4.2 To use the EspressIf SDK Web Server API, which uses event callbacks, use this define in the sketch:

define SVR_TYPE SVR_HTTP_SDK

4.3 The MQTT server is enabled in the following sketch line:

define MQTT_SVR_ENABLE 1

<strong>Operation:</strong>

While not necessary, the code assumes an LED is connected to GPIO16. This LED is ON upon 
power-up and is turned OFF once initialization completes.


Web Server Performance Test:

Open the html file "mqtt_server.html" in a web browser to test the Web Server.

Click on the "Request via MQTT" button to measure the request to reply time of the MQTT Server.

Click on the "Request via HTTP" button to measure the request to reply time of the HTTP Server.

Either server will respond to 3 requests:

1. /?request=GetSensors
2. /?request=LedOn
3. /?request=LedOff

For the "GetSensors" request, a JSON string will be returned with the sensor values in this format:

{
"Ain0":"316.00",
"Ain1":"326.00",
"Ain2":"325.00",
"Ain3":"314.00",
"Ain4":"316.00",
"Ain5":"163.00",
"Ain6":"208.00",
"Ain7":"333.00",
"SYS_Heap":"25408",
"SYS_Time":"26"
}

The servers will also respond to requests to turn the LED, if connected, on and off.

To turn on, enter the request:

/?request=LedOn

And click on one of the "Request via..." buttons.

To turn off, enter the request:

?request=LedOff

And click on one of the "Request via..." buttons.

The request-to-reply time is calculated and displayed for each of these 3 requests
