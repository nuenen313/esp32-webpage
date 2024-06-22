#include <WiFi.h>
#include <WebServer.h>
#include <LSM6DSRSensor.h>
#include <SD.h>
#include <SPI.h>
#include <RingBuf.h>

const char* ssid = "your-ssid";
const char* password = "your-password";

WebServer server(80);

#define SerialPort Serial
#define VSPI_MISO   19
#define VSPI_MOSI   23
#define VSPI_SCLK   18
#define VSPI_SS     5
#define SD_CS       4
#define BUFFER_SIZE 255

SPIClass dev_spi(VSPI);
LSM6DSRSensor AccGyr(&dev_spi, 5);
struct AccelData {
  float axes[3];
};
RingBuf<AccelData, BUFFER_SIZE> bufferA;
RingBuf<AccelData, BUFFER_SIZE> bufferB;
bool SaveData = false;
bool currentBuffer = true;
String currentFilename = "/data.csv";

// Function prototypes
void printDirectory(File dir, int numTabs);
void createFile(const char* filename);
void saveBufferToCSV(RingBuf<AccelData, BUFFER_SIZE>& buffer);
void processBuffer();

void handleRoot() {
  String html = "<html>\
  <head>\
    <title>LMS6DSR Measurement Control</title>\
    <style>\
      body { background-color: #333; color: #fff; font-family: 'Arial', sans-serif; text-align: center; }\
      h1, h2 { color: #f0f0f0; }\
      form { margin-bottom: 20px; display: inline-block; text-align: left; }\
      input[type=text], input[type=submit] { padding: 12px; font-size: 18px; }\
      input[type=submit] { background-color: #4CAF50; color: white; border: none; cursor: pointer; }\
      ul { list-style-type: none; padding: 0; text-align: left; display: inline-block; }\
      li { margin-bottom: 15px; }\
      a { color: #4CAF50; text-decoration: none; margin-left: 10px; font-size: 16px; }\
      a:hover { text-decoration: underline; }\
    </style>\
  </head>\
  <body>\
    <div style=\"width: 80%; margin: 0 auto;\">\
      <h1>LSM6DSR Measurement Control</h1>\
      <form action=\"/start\" method=\"GET\">\
        Filename: <input type=\"text\" name=\"filename\" value=\"data.csv\">\
        <input type=\"submit\" value=\"Start Measurement\">\
      </form>\
      <h2>Files on SD Card</h2>\
      <ul>";
  
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break;
    }
    if (!entry.isDirectory()) {
      html += "<li>" + String(entry.name()) + " (" + String(entry.size()) + " bytes)";
      html += " <a href=\"/download?name=" + String(entry.name()) + "\">Download</a>";
      html += " <a href=\"/delete?name=" + String(entry.name()) + "\">Delete</a></li>";
    }
    entry.close();
  }
  html += "</ul>\
    </div>\
  </body>\
  </html>";
  server.send(200, "text/html", html);
}


void handleStart() {
  if (server.hasArg("filename")) {
    currentFilename = "/" + server.arg("filename");
    createFile(currentFilename.c_str());
    SaveData = true;
    unsigned long measurementDuration = 10000;
    unsigned long startTime = millis();
    String initialHtml = "<html>\
    <head>\
      <title>LMS6DSR Measurement Control</title>\
      <style>\
        body { background-color: #333; color: #fff; font-family: 'Arial', sans-serif; text-align: center; }\
        h1 { color: #4CAF50; font-size: 32px; }\
        .spinner { margin: 20px auto; width: 40px; height: 40px; position: relative; text-align: center; }\
        .spinner div { width: 18px; height: 18px; background-color: #4CAF50; position: absolute; left: 1px; bottom: 1px; animation: spinner 1.2s cubic-bezier(0.5, 0, 0.5, 1) infinite; }\
        @keyframes spinner { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }\
        .hidden { display: none; } /* CSS class to hide elements */\
        a { color: #4CAF50; text-decoration: none; margin-top: 20px; font-size: 18px; }\
        a:hover { text-decoration: underline; }\
      </style>\
    </head>\
    <body>\
      <h1 id=\"status\">Measurement in Progress</h1>\
      <div id=\"spinner\" class=\"spinner\"><div></div></div>\
      <script>\
        var measurementDuration = " + String(measurementDuration) + ";\
        var startTime = Date.now();\
        function checkStatus() {\
          var elapsed = Date.now() - startTime;\
          if (elapsed >= measurementDuration) {\
            document.getElementById('status').innerHTML = 'Measurement Done';\
            document.getElementById('spinner').classList.add('hidden');\
            var link = document.createElement('a');\
            link.href = '/';\
            link.innerHTML = 'Go Back';\
            document.body.appendChild(link);\
          } else {\
            setTimeout(checkStatus, 1000);\
          }\
        }\
        checkStatus();\
      </script>\
    </body>\
    </html>";
    
    while (millis() - startTime < measurementDuration) {
      if (!SaveData) break;
      server.send(200, "text/html", initialHtml);
      processBuffer();
    }
    SaveData = false;
    currentBuffer = true;
    server.send(200, "application/json", "{\"done\": true}");
    handleOnDone();
  } else {
    server.send(400, "text/html", "<html><body><h1 style=\"color: #ff6347;\">Error</h1><p style=\"font-size: 20px;\">Filename not provided</p><a href=\"/\">Go Back</a></body></html>");
  }
}

void handleOnDone() {
  String doneHtml = "<html>\
  <head>\
    <title>LMS6DSR Measurement Control</title>\
    <style>\
      body { background-color: #333; color: #fff; font-family: 'Arial', sans-serif; text-align: center; }\
      h1 { color: #4CAF50; font-size: 32px; }\
      a { color: #4CAF50; text-decoration: none; margin-top: 20px; font-size: 18px; }\
      a:hover { text-decoration: underline; }\
    </style>\
  </head>\
  <body>\
    <h1>Measurement Done</h1>\
    <a href=\"/\">Go Back</a>\
  </body>\
  </html>";

  server.send(200, "text/html", doneHtml);
}

void handleDownload() {
  if (server.hasArg("name")) {
    String filename = "/" + server.arg("name");
    File file = SD.open(filename, FILE_READ);
    if (file) {
      server.setContentLength(file.size());
      server.sendHeader("Content-Type", "application/octet-stream");
      server.sendHeader("Content-Disposition", "attachment; filename=" + server.arg("name"));
      server.sendHeader("Connection", "close");
      server.streamFile(file, "application/octet-stream");
      file.close();
    } else {
      String html = "<html>\
      <head>\
        <title>Measurement Control</title>\
        <style>\
          body { background-color: #333; color: #fff; font-family: 'Arial', sans-serif; text-align: center; }\
          h1 { color: #ff6347; font-size: 32px; }\
          a { color: #4CAF50; text-decoration: none; margin-top: 20px; font-size: 18px; }\
          a:hover { text-decoration: underline; }\
        </style>\
      </head>\
      <body>\
        <h1>File not found</h1>\
        <a href=\"/\">Go Back</a>\
      </body>\
      </html>";
      server.send(404, "text/html", html);
    }
  } else {
    server.send(400, "text/html", "<html><body><h1 style=\"color: #ff6347;\">Error</h1><p style=\"font-size: 20px;\">Filename not provided</p><a href=\"/\">Go Back</a></body></html>");
  }
}

void handleDelete() {
  if (server.hasArg("name")) {
    String filename = "/" + server.arg("name");
    if (SD.exists(filename)) {
      SD.remove(filename);
      String html = "<html>\
      <head>\
        <title>Measurement Control</title>\
        <style>\
          body { background-color: #333; color: #fff; font-family: 'Arial', sans-serif; text-align: center; }\
          h1 { color: #4CAF50; font-size: 32px; }\
          a { color: #4CAF50; text-decoration: none; margin-top: 20px; font-size: 18px; }\
          a:hover { text-decoration: underline; }\
        </style>\
      </head>\
      <body>\
        <h1>File Deleted</h1>\
        <a href=\"/\">Go Back</a>\
      </body>\
      </html>";
      server.send(200, "text/html", html);
    } else {
      server.send(404, "text/html", "<html><body><h1 style=\"color: #ff6347;\">Error</h1><p style=\"font-size: 20px;\">File not found</p><a href=\"/\">Go Back</a></body></html>");
    }
  } else {
    server.send(400, "text/html", "<html><body><h1 style=\"color: #ff6347;\">Error</h1><p style=\"font-size: 20px;\">Filename not provided</p><a href=\"/\">Go Back</a></body></html>");
  }
}

void setup() {
  SPI.begin(23, 19, 18, 5);
  pinMode(VSPI_SS, OUTPUT);
  digitalWrite(VSPI_SS, HIGH); 
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  
  dev_spi.begin();
  AccGyr.begin();
  AccGyr.Set_X_FS(8); // +- 8g 
  AccGyr.Set_X_ODR_With_Mode(6667.0f, LSM6DSR_ACC_HIGH_PERFORMANCE_MODE); // max available ODR
  AccGyr.Enable_X();

  SerialPort.begin(115200);
  delay(1000);
  if (!SD.begin(SD_CS)) {
    Serial.println("Failed to initialize SD card!");
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Print the IP address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Define web server routes
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/done", handleOnDone);
  server.on("/download", handleDownload);
  server.on("/delete", handleDelete);

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  if (SaveData) {
    processBuffer();
  }
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print(' ');
      delay(50);
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("  ");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void createFile(const char* filename) {
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.println("Acceleration X,Acceleration Y,Acceleration Z");
    dataFile.close();
    Serial.println("New file created.");
  } else {
    Serial.println("Error creating file.");
  }
}

void saveBufferToCSV(RingBuf<AccelData, BUFFER_SIZE>& bufferBuffer) {
  Serial.println(currentFilename.c_str());
  if (!SD.exists(currentFilename.c_str())) {
    createFile(currentFilename.c_str());
    delay(100);
  }
  File dataFile = SD.open(currentFilename.c_str(), FILE_APPEND);
  if (dataFile) {
    Serial.println("Saving...");
    for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
      AccelData data = bufferBuffer[i];
      dataFile.print(data.axes[0], 6);
      dataFile.print(", ");
      dataFile.print(data.axes[1], 6);
      dataFile.print(", ");
      dataFile.print(data.axes[2], 6);
      dataFile.println();
    }
    bufferBuffer.clear();
    dataFile.close();
    Serial.println("Buffer contents saved");
  } else {
    Serial.println("Failed to open file for writing");
  }
}

void processBuffer() {
  static float lastAxes[3];
  AccelData accelData;
  AccGyr.Get_X_Axes(accelData.axes);
  if (memcmp(&accelData.axes, &lastAxes, sizeof(lastAxes)) != 0) {
    if (currentBuffer) {
      bufferA.pushOverwrite(accelData);
    } else {
      bufferB.pushOverwrite(accelData);
    }
    memcpy(lastAxes, accelData.axes, sizeof(lastAxes));
  }
  if (currentBuffer && bufferA.size() == BUFFER_SIZE) {
    Serial.println("Saving");
    currentBuffer = !currentBuffer;
    saveBufferToCSV(bufferA);
  } else if (!currentBuffer && bufferB.size() == BUFFER_SIZE) {
    Serial.println("Saving");
    currentBuffer = !currentBuffer;
    saveBufferToCSV(bufferB);
  }
}
