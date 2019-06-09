    iButton access control 
====================

 iButton Access Control project

    About this project
 This software is developed on ESP32 (Espressif) device. \n
 The task of this project is to create an online access control device.
 The authentication is realized with iButton (Maxim Integrated) keys and can be restrict in time.
 It means that the device only allows access when the given person's key can be found in the local database
 and the time restriction setting - belongs to each person - matches the current time.

 Time descriptor are based on (UNIX) crontab and used to store time settings related to each keys.
 This device works with a local database stored in flash memory. It consists of key-cron entries.
 
    Architecture: ESP32\n
    Used framework: ESP-IDF V3.0
