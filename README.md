# esp32-webserver-accelerometer-data-saving
 ESP32 arduino code for creating csv files on the SD card, saving data to the file and downloading/deleting the files through a webserver. Tested on LOLIN D32 PRO and LSM6DSR.

 The code creates a websever with an IP that is printed to the serial port. The server allows the user to start a measurement with saving the data to a .csv file on a SD card connected to the ESP32, as well as downloading and deleting the files that are already present on the SD card:
 
 ![server](https://github.com/user-attachments/assets/af948f2f-504d-405e-b91a-566dc917f71c)
