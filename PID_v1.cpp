/**********************************************************************************************
 * Arduino PID Library - Version 1.2.1
 * by Brett Beauregard <br3ttb@gmail.com> brettbeauregard.com
 *
 * This Library is licensed under the MIT License
 **********************************************************************************************/

#if ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

#include <PID_v1.h>

/*Constructor (...)*********************************************************
 *    The parameters specified here are those for for which we can't set up
 *    reliable defaults, so we need to have the user set them.
 ***************************************************************************/
PID::PID(double *Input, double *Output, double *Setpoint,
         double Kp, double Ki, double Kd, int POn, int ControllerDirection)
{
   myOutput = Output;
   myInput = Input;
   mySetpoint = Setpoint;
   inAuto = false;

   PID::SetOutputLimits(0, 255);			   //default output limit corresponds to
                                          //the arduino pwm limits
   PID::SetIntegratorLimits(-100, 100);   //set default integrator limits

   SampleTime = 100;							   //default Controller Sample Time is 0.1 seconds

   PID::SetControllerDirection(ControllerDirection);
   PID::SetTunings(Kp, Ki, Kd, POn);

   lastTime = millis()-SampleTime;
}

/*Constructor (...)*********************************************************
 *    To allow backwards compatability for v1.1, or for people that just want
 *    to use Proportional on Error without explicitly saying so
 ***************************************************************************/
PID::PID(double *Input, double *Output, double *Setpoint,
         double Kp, double Ki, double Kd, int ControllerDirection)
    : PID::PID(Input, Output, Setpoint, Kp, Ki, Kd, P_ON_E, ControllerDirection)
{
}

/* Compute() **********************************************************************
 *     This, as they say, is where the magic happens.  this function should be called
 *   every time "void loop()" executes.  the function will decide for itself whether a new
 *   pid Output needs to be computed.  returns true when the output is computed,
 *   false when nothing has been done.
 **********************************************************************************/
bool PID::Compute()
{
   if(!inAuto) return false;
   unsigned long now = millis();
   unsigned long timeChange = (now - lastTime);
   if (timeChange>=SampleTime)
   {
      // Compute all the working error variables
      double input = *myInput;
      double error = *mySetpoint - input;
   
      //Integral Part      
      //added: don't let I part sum grow if output is already at max (from e.g. P alone), c.f. https://github.com/br3ttb/Arduino-PID-Library/issues/76
      if (!pOnE || (*myOutput < outMax - 0.01 && *myOutput > outMin + 0.01)) {
         integrator += (ki * error);
      }

      //use Exponentially weighted moving average as low pass filter of input data
      double oldFiltered = lastFilteredInput;

      //calc new IIR filtered value
      lastFilteredInput = filterAlpha * lastFilteredInput + (1-filterAlpha) * input;

      //calc filtered input differential
      //(the controller uses negative of derived input instead of derived error, since it's equal when assuming setpoint is constant - solves derivative kick)
      double dInput = 0;
      if (pOnE) {
         dInput = (lastFilteredInput - oldFiltered) / (SampleTime/1000);
      } else {
         //PonM seems to need sensor noise to even start, so use unfiltered
         dInput = input - lastInput;
      }

      // Add Proportional on Measurement, if P_ON_M is specified
      // (kp is used in additional D part (and no P part anymore) but it affects only the I sum,
      // not the output which includes the other D part)
      if (!pOnE) integrator-= kp * dInput;

      // Apply output limits to I sum (worst case anti-windup, see http://brettbeauregard.com/blog/2011/04/improving-the-beginner%e2%80%99s-pid-reset-windup/)
      if (integrator > outMax) integrator = outMax;
      else if (integrator < outMin) integrator = outMin;

      //limit integrator to its own limits (only if in normal PID mode, not PonM)
      if (pOnE) {
         if (integrator > integratorMax) integrator = integratorMax;
         else if (integrator < integratorMin) integrator = integratorMin;
      }

      // Add Proportional on Error, if P_ON_E is specified
      double output;
      if (pOnE) output = kp * error;
      else output = 0;

      // Add D part to integral sum and add to output 
      output += integrator - kd * dInput;

      // Limit overall output again
      if (output > outMax) output = outMax;
      else if (output < outMin) output = outMin;
      *myOutput = output;

      // Remember some variables for next time
      lastFilteredDifferential = dInput;
      lastInput = input;
      lastPPart = pOnE ? kp * error : 0; 
      lastDPart = - kd * dInput;
      lastError = error;
      lastTime = now;
      return true;
   }
   else return false;
}

/* SetTunings(...)*************************************************************
 * This function allows the controller's dynamic performance to be adjusted.
 * it's called automatically from the constructor, but tunings can also
 * be adjusted on the fly during normal operation
 ******************************************************************************/
void PID::SetTunings(double Kp, double Ki, double Kd, int POn)
{
   if (Kp<0 || Ki<0 || Kd<0) return;

   pOn = POn;
   pOnE = POn == P_ON_E;

   dispKp = Kp; dispKi = Ki; dispKd = Kd;

   double SampleTimeInSec = ((double)SampleTime)/1000;
   kp = Kp;
   ki = Ki * SampleTimeInSec;
   kd = Kd / SampleTimeInSec;

   if (controllerDirection == REVERSE)
   {
      kp = (0 - kp);
      ki = (0 - ki);
      kd = (0 - kd);
   }

   if (Ki == 0) {integrator = 0;}
}

/* SetTunings(...)*************************************************************
 * Set Tunings using the last-rembered POn setting
 ******************************************************************************/
void PID::SetTunings(double Kp, double Ki, double Kd){
   SetTunings(Kp, Ki, Kd, pOn); 
}

/* SetSampleTime(...) *********************************************************
 * sets the period, in Milliseconds, at which the calculation is performed
 ******************************************************************************/
void PID::SetSampleTime(int NewSampleTime)
{
   if (NewSampleTime > 0)
   {
      double ratio  = (double)NewSampleTime / (double)SampleTime;
      ki *= ratio;
      kd /= ratio;
      SampleTime = (unsigned long)NewSampleTime;
   }
}

void PID::SetSmoothingFactor(double alpha) {
   filterAlpha = alpha;
}

/* SetOutputLimits(...)****************************************************
 *     This function will be used far more often than SetInputLimits.  while
 *  the input to the controller will generally be in the 0-1023 range (which is
 *  the default already,)  the output will be a little different.  maybe they'll
 *  be doing a time window and will need 0-8000 or something.  or maybe they'll
 *  want to clamp it from 0-125.  who knows.  at any rate, that can all be done
 *  here.
 **************************************************************************/
void PID::SetOutputLimits(double Min, double Max)
{
   if(Min >= Max) return;
   outMin = Min;
   outMax = Max;

   if(inAuto)
   {
      if (*myOutput > outMax) *myOutput = outMax;
      else if (*myOutput < outMin) *myOutput = outMin;

      if (integrator > outMax) integrator = outMax;
      else if (integrator < outMin) integrator = outMin;
   }
}

void PID::SetIntegratorLimits(double Min, double Max)
{
   if (Min >= Max) return;
   integratorMin = Min;
   integratorMax = Max;

   if (inAuto)
   {
      if (integrator > integratorMax) integrator = integratorMax;
      else if (integrator < integratorMin) integrator = integratorMin;
   }
}

/* SetMode(...)****************************************************************
 * Allows the controller Mode to be set to manual (0) or Automatic (non-zero)
 * when the transition from manual to auto occurs, the controller is
 * automatically initialized
 ******************************************************************************/
void PID::SetMode(int Mode)
{
   bool newAuto = (Mode == AUTOMATIC);
   if (newAuto && !inAuto)
   {  // we just went from manual to auto
      PID::Initialize();
   }
   inAuto = newAuto;
}

/* Initialize()****************************************************************
 *	does all the things that need to happen to ensure a bumpless transfer
 *  from manual to automatic mode.
 ******************************************************************************/
void PID::Initialize()
{
   integrator = *myOutput;
   lastInput = lastFilteredInput = *myInput;

   if (integrator > outMax) integrator = outMax;
   else if (integrator < outMin) integrator = outMin;
}

/* SetControllerDirection(...)*************************************************
 * The PID will either be connected to a DIRECT acting process (+Output leads
 * to +Input) or a REVERSE acting process(+Output leads to -Input.)  we need to
 * know which one, because otherwise we may increase the output when we should
 * be decreasing.  This is called from the constructor.
 ******************************************************************************/
void PID::SetControllerDirection(int Direction)
{
   if (inAuto && Direction != controllerDirection)
   {
      kp = (0 - kp);
      ki = (0 - ki);
      kd = (0 - kd);
   }
   controllerDirection = Direction;
}

/* Status Funcions*************************************************************
 * Just because you set the Kp=-1 doesn't mean it actually happened.  these
 * functions query the internal state of the PID.  they're here for display
 * purposes.  this are the functions the PID Front-end uses for example
 ******************************************************************************/
double PID::GetKp(){ return  dispKp; }
double PID::GetKi(){ return  dispKi;}
double PID::GetKd(){ return  dispKd;}
int PID::GetMode(){ return  inAuto ? AUTOMATIC : MANUAL;}
int PID::GetDirection() { return controllerDirection; }
bool PID::GetPonE() { return pOnE; }
double PID::GetDeltaInput() { return lastFilteredDifferential; }
double PID::GetInputError() { return lastError; }
double PID::GetLastPPart() { return lastPPart; }
double PID::GetLastIPart() { return integrator; }
double PID::GetLastDPart() { return lastDPart; }
