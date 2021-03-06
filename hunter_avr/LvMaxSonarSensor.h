/**
 *
 * LvMaxSonarSensor.h
 *
 * Code for reading the sonar sensor (LV-MaxSonar-EZ), based on sample
 *.code by Bruce Allen, dated 23/07/09.
 * The speed of sound at sea level is 29.3866996 usec/cm (74.6422169 usec/inch),
 * but the pulses we'll be reading from the LV-MaxSonar-EX are 147 usec/inch,
 * according to the datasheet (XXX - This value should be more like 149.28 usec/inch, right?).
 * 
 * Datasheet:  http://maxbotix.com/documents/LV-MaxSonar-EZ_Datasheet.pdf
 *
 */

#ifndef _LV_MAX_SONAR_SENSOR_H_
#define _LV_MAX_SONAR_SENSOR_H_

#include <Arduino.h>

// pulse width modulation
#define LEFT_COLLISION_AVOID_SENSOR_PW_PIN     22
#define RIGHT_COLLISION_AVOID_SENSOR_PW_PIN    24

// analog (permits cooperation between sensors)
#define LEFT_COLLISION_AVOID_SENSOR_AN_PIN     A14
#define RIGHT_COLLISION_AVOID_SENSOR_AN_PIN    A15
// TODO: we can tie all the RX pins on the sensors to this digital pin in order
// to trigger simultaneous readings.  Go high to start getting readings.
#define RANGE_READING_SYNC_PIN                 28


// pulse width modulation
#define SCALE_USEC_PER_INCH          149.28      // is 147, according to the datasheet
#define SCALE_USEC_PER_CM             58.77      
#define CM_PER_INCH                    2.54

// the maximum range is supposedly 6.45 meters, but we'll say 2...
#define CM_MAX_RANGE_USEC_TIMEOUT (SCALE_USEC_PER_CM * 200)

// for collision avoidance
#define DEFAULT_MINIMUM_RANGE_CM      18  // how close is too close for comfort?  Min range is ~15cm...
#define NO_MINIMUM_RANGE_CM           -1  // no minimum range to worry about


class LvMaxSonarSensor
{
  private:
    typedef enum
    {
      PWM_MODE,
      ANALOG_MODE,
      UNDEFINED
    } SensorMode;
    
    SensorMode sensorMode = UNDEFINED;
  
  protected:
    // pin for reading pulses (pulse of 149.28usec/inch) or for reading voltages (~9.8mV/inch)
    int readPin;  
    long lastMeasurementCm = -1;
  
  public:
    LvMaxSonarSensor();
    
    void setReadPin(int pinNumberIn, SensorMode sensorModeIn);
    
    // TODO - implement these if we go synchronized analog:
    void enableReadPin();
    void disableReadPin();
        
    long getDistanceCm();  // return the distance in cm
    long getLastMeasurementCm();
};


class LvMaxSonarCollisionAvoidanceSensor : public LvMaxSonarSensor
{
  private:
    long minRangeCm = DEFAULT_MINIMUM_RANGE_CM;
    bool tooClose = false;

  public:
    LvMaxSonarCollisionAvoidanceSensor();
    
    void setMinimumRange(int minRangeCmIn);
    
    long getDistanceCm();  // return the distance in cm, and set the "tooClose" flag as appropriate
                             
    bool isTooClose();  // are we too close to some object?

};

#endif  /* _LV_MAX_SONAR_SENSOR_H_ */

