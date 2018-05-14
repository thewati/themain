#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <dht.h>
#include "ACS712.h"
#include <SoftwareSerial.h>

// size of buffer used to capture HTTP requests
#define REQ_BUF_SZ   20
//Analogue pins
#define dht_apin A4

dht DHT;

/************************Hardware Related Macros for Gas************************************/
#define         MQ_PIN                       (0)     //define which analog input channel you are going to use
#define         RL_VALUE                     (5.1)     //define the load resistance on the board, in kilo ohms
#define         RO_CLEAN_AIR_FACTOR          (9.83)  //RO_CLEAR_AIR_FACTOR=(Sensor resistance in clean air)/RO,
                                                     //which is derived from the chart in datasheet
/***********************Software Related Macros for Gas************************************/
#define         CALIBARAION_SAMPLE_TIMES     (50)    //define how many samples you are going to take in the calibration phase
#define         CALIBRATION_SAMPLE_INTERVAL  (500)   //define the time interal(in milisecond) between each samples in the
                                                     //cablibration phase
#define         READ_SAMPLE_INTERVAL         (50)    //define how many samples you are going to take in normal operation
#define         READ_SAMPLE_TIMES            (5)     //define the time interal(in milisecond) between each samples in 
                                                     //normal operation
 
/**********************Application Related Macro for Gas**********************************/
#define         GAS_SMOKE                    (2)
 
/*****************************Globals***********************************************/
float           SmokeCurve[3] ={2.3,0.53,-0.44};    //two points are taken from the curve. 
                                                    //with these two points, a line is formed which is "approximately equivalent" 
                                                    //to the original curve.
                                                    //data format:{ x, y, slope}; point1: (lg200, 0.53), point2: (lg10000,  -0.22)                                                     
float           Ro           =  10;                 //Ro is initialized to 10 kilo ohms

ACS712 sensor(ACS712_20A, A5);

//GSM configuration in the bottom three lines
//SIM800 TX is connected to Arduino D
#define SIM800_TX_PIN 1
//SIM800 RX is connected to Arduino D
#define SIM800_RX_PIN 0

//Create software serial object to communicate with SIM800
SoftwareSerial serialSIM800(SIM800_TX_PIN,SIM800_RX_PIN);

//int textSent = 0; //state variable for sending out a single sms

// MAC address from Ethernet shield sticker under board
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 11, 177);   // IP address, may need to change depending on network
EthernetServer server(80);       // create a server at port 80
File webFile;                    // handle to files on SD card
char HTTP_req[REQ_BUF_SZ] = {0}; // buffered HTTP request stored as null terminated string
char req_index = 0;              // index into HTTP_req buffer

//four lines added below
int BULB = 8;              //LED to turn on/off ...RED=
int FAN = 7;              //pin to turn on/off FAN
int ALARM = 6;            //pin to turn on/off ALARM



String HTTP_reqe;          // stores the HTTP request

void setup()
{
    // disable Ethernet chip
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);
    
    Serial.begin(9600);       // for debugging
    
    // initialize SD card
    //Serial.println("Initializing SD card..."); //commented out
    if (!SD.begin(4)) {
        Serial.println("ERROR - SD card initialization failed!");  
        return;    // init failed
    }
    //Serial.println("SUCCESS - SD card initialized.");  //commented out
    // check for login.htm file
    if (!SD.exists("login.htm")) {
        Serial.println("ERROR - Can't find login.htm file!");
        return;  // can't find login file
    }
    //Serial.println("SUCCESS - Found login.htm file.");  //commented out

    Ethernet.begin(mac, ip);  // initialize Ethernet device
    server.begin();           // start to listen for clients

    //5 lines added below
    HTTP_reqe = "";
  //Set RED, FAN and ALARM to output
  pinMode(BULB, OUTPUT);
  pinMode(FAN, OUTPUT);
  pinMode(ALARM, OUTPUT);

  Ro = MQCalibration(MQ_PIN);
  
  sensor.calibrate();   // Ensure that no current (in ACS712) flows through the sensor at this moment to ensure accuracy
}

void loop()
{
  //temperature,smoke, power function calls

  Serial.print(TempReading());
  Serial.print(",");
  Serial.print(MQGetGasPercentage(MQRead(MQ_PIN)/Ro,GAS_SMOKE));
  Serial.print(",");
  Serial.println(PowerReading());
  
  if(MQGetGasPercentage(MQRead(MQ_PIN)/Ro,GAS_SMOKE)>0)
  {
    tone(ALARM, 1000);
   // if(textSent == 0)
    //{
      firesms();
      //textSent = 1;
    //}
  }
  else
  {
    noTone(ALARM);
  }

  delay(5000);


    EthernetClient client = server.available();  // try to get client

    if (client) {  // got client?
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {   // client data available to read
                char c = client.read(); // read 1 byte (character) from client
                // buffer first part of HTTP request in HTTP_req array (string)
                // leave last element in array as 0 to null terminate string (REQ_BUF_SZ - 1)
                if (req_index < (REQ_BUF_SZ - 1)) {
                    HTTP_req[req_index] = c;          // save HTTP request character
                    req_index++;
                }

                //added the 2 lines below
                if ( HTTP_reqe.length() < 80)
                HTTP_reqe += c;  // save the HTTP request 1 char at a time
                
                
                Serial.print(c);    // print HTTP request character to serial monitor
                // last line of client request is blank and ends with \n
                // respond to client only after last line received
                if (c == '\n' && currentLineIsBlank) {
                    // send a standard http response header
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connnection: close");
                    client.println();
                    // open requested web page file
                    if (StrContains(HTTP_req, "GET / ")
                                 || StrContains(HTTP_req, "GET /login.htm")) {
                        webFile = SD.open("login.htm");        // open web page file
                    }
                    else if (StrContains(HTTP_req, "GET /panel.htm")) {
                        webFile = SD.open("panel.htm");        // open web page file
                    }

                    //added the block below
                    if (HTTP_reqe.indexOf("ajaxrefresh") >= 0 ) {
                    ajaxRequest(client);  //update the analog values
                    break;
                    }
                    else if (HTTP_reqe.indexOf("ledstatus") >= 0 ) {
                    ledChangeStatus(client); //change the LED state
                    break;
                    }
                    else if (HTTP_reqe.indexOf("fanstatus") >= 0 ) {
                    fanChangeStatus(client); //change the LED state
                    break;
                    }
                    else if (HTTP_reqe.indexOf("alarmstatus") >= 0 ) {
                    alarmChangeStatus(client); //change the LED state
                    break;
                    }
                    
                    // send web page to client
                    if (webFile) {
                        while(webFile.available()) {
                            client.write(webFile.read());
                        }
                        webFile.close();
                    }
                    // reset buffer index and all buffer elements to 0
                    req_index = 0;
                    StrClear(HTTP_req, REQ_BUF_SZ);
                    break;
                }
                // every line of text received from the client ends with \r\n
                if (c == '\n') {
                    // last character on line of received text
                    // starting new line with next character read
                    currentLineIsBlank = true;
                } 
                else if (c != '\r') {
                    // a text character was received from client
                    currentLineIsBlank = false;
                }
            } // end if (client.available())
        } // end while (client.connected())
        delay(1);      // give the web browser time to receive the data
        client.stop(); // close the connection

        //added the line below
        HTTP_reqe = "";
        
    } // end if (client)
    
}

// sets every element of str to 0 (clears array)
void StrClear(char *str, char length)
{
    for (int i = 0; i < length; i++) {
        str[i] = 0;
    }
}

// searches for the string sfind in the string str
// returns 1 if string found
// returns 0 if string not found
char StrContains(char *str, char *sfind)
{
    char found = 0;
    char index = 0;
    char len;

    len = strlen(str);
    
    if (strlen(sfind) > len) {
        return 0;
    }
    while (index < len) {
        if (str[index] == sfind[found]) {
            found++;
            if (strlen(sfind) == found) {
                return 1;
            }
        }
        else {
            found = 0;
        }
        index++;
    }

    return 0;
}

//added functions below

//send state of power and temp to the web browser
void ajaxRequest(EthernetClient client)
{
    client.print("Power Consumption is ");
    client.print(PowerReading());
    client.print(" W");
    client.println("<br />");
    
    client.print("Temperature is ");
    client.print(TempReading());
    client.print(" C");
    client.println("<br />");
}

void ledChangeStatus(EthernetClient client)
{
  int state = digitalRead(BULB);
  Serial.println(state);
  if (state == 1) {
    digitalWrite(BULB, LOW);
    client.print("OFF");
  }
  else {
    digitalWrite(BULB, HIGH);
    client.print("ON");
  }
}

void fanChangeStatus(EthernetClient client)
{
  int state = digitalRead(FAN);
  Serial.println(state);
  if (state == 1) {
    digitalWrite(FAN, LOW);
    client.print("ON");
  }
  else {
    digitalWrite(FAN, HIGH);
    client.print("OFF");
  }
}

void alarmChangeStatus(EthernetClient client)
{
  int state = digitalRead(ALARM);
  Serial.println(state);
  if (state == 1) {
    digitalWrite(ALARM, LOW);
    noTone(ALARM);
    client.print("OFF");
  }
  else {
    digitalWrite(ALARM, HIGH);
    tone(ALARM, 1000);
    client.print("ON");
  }
}

//temperature function
int TempReading()
{
  DHT.read11(dht_apin);
  int temp = int(DHT.temperature);
  return temp;
}

//5 smoke functions below
float MQResistanceCalculation(int raw_adc)
{
  return ( ((float)RL_VALUE*(1023-raw_adc)/raw_adc));
}

float MQCalibration(int mq_pin)
{
  int i;
  float val=0;
 
  for (i=0;i<CALIBARAION_SAMPLE_TIMES;i++) {            //take multiple samples
    val += MQResistanceCalculation(analogRead(mq_pin));
    delay(CALIBRATION_SAMPLE_INTERVAL);
  }
  val = val/CALIBARAION_SAMPLE_TIMES;                   //calculate the average value
 
  val = val/RO_CLEAN_AIR_FACTOR;                        //divided by RO_CLEAN_AIR_FACTOR yields the Ro 
                                                        //according to the chart in the datasheet 
 
  return val; 
}

float MQRead(int mq_pin)
{
  int i;
  float rs=0;
 
  for (i=0;i<READ_SAMPLE_TIMES;i++) {
    rs += MQResistanceCalculation(analogRead(mq_pin));
    delay(READ_SAMPLE_INTERVAL);
  }
 
  rs = rs/READ_SAMPLE_TIMES;
 
  return rs;  
}

int MQGetGasPercentage(float rs_ro_ratio, int gas_id)
{
  if ( gas_id == GAS_SMOKE ) {
     return MQGetPercentage(rs_ro_ratio,SmokeCurve);
  }    
 
  return 0;
}

int  MQGetPercentage(float rs_ro_ratio, float *pcurve)
{
  return (pow(10,( ((log(rs_ro_ratio)-pcurve[1])/pcurve[2]) + pcurve[0])));
}

//Function to calculate power
float PowerReading()
{
  // We use 230V because it is the common standard in Malawi
  // Change to your local, if necessary
  float U = 230;

  // To measure current we need to know the frequency of current
  // By default 50Hz is used, but you can specify own, if necessary
  float I = sensor.getCurrentAC();

  // To calculate the power we need voltage multiplied by current
  float P = U * I;
  return P;
}


//Function to send SMS below
void firesms()
{
  serialSIM800.begin(9600);
  delay(1000);
  
//Set SMS format to ASCII
  serialSIM800.write("AT+CMGF=1\r\n");
  delay(1000);
 
  //Send new SMS command and message number
  serialSIM800.write("AT+CMGS=\"0881381771\"\r\n");
  delay(1000);
   
  //Send SMS content
  serialSIM800.write("Silas Nyirenda Office Block is burning down at Khondowe");
  delay(1000);
   
  //Send Ctrl+Z / ESC to denote SMS message is complete
  serialSIM800.write((char)26);
  delay(1000);
     
  Serial.println("SMS Sent!");
}

