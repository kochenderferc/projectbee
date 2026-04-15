# projectbee
Arduino code for a multi-sensor device for beekeepers to assist in tracking bee colony health.

## Code overview
We set base values based upon information we gained from meeting with beekeepers and researchers. We compare those values against the measured values, which we measure using a piezo sensor (for vibration) and a SEN54 sensor (measures particulate matter, VOC, humidity, and temperature). If the measured values are outside of the range for the baselines, it’ll set a warning for the user and print all measured values into a CSV file. This allows easy information access to exactly what is going wrong with the hive.

## Test code overview
This allows us to test to ensure the SEN54 sensor is working accurately, using values that are in and out of the safe range.
