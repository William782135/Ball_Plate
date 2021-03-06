
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include "pid.h"
#include "../include/ball_plate.h"
#include "../include/micro_maestro.h"

void wait_for_deltat(struct timeval *tim, double *t_curr, double *t_past, double *new_delta, double required_delta);  //no sleep (maintain cpu time)

void stable_mode()
{
	int fd = init_maestro();
	int target; //Micro Maestro target value
	double deltaT_x, deltaT_y;
	double t_x_curr, t_x_past, t_y_curr, t_y_past, t_curr;
	struct timeval tim;
	double r_act = 0;
	float x_pos[] = {-0.07, 0.07, 0.07, -0.07};
	float y_pos[] = {0.07, 0.07, -0.07, -0.07};
	int pos_current = 0;
	double du_x, du_x_past, du_y, du_y_past, ddu_x, ddu_y;

	maestroSetSpeed(fd, 20);
	maestroSetTarget(fd, 1, 4*Y_SERVO_CENTRE);
	usleep(20000);
	maestroSetTarget(fd, 0, 4*X_SERVO_CENTRE);	
	usleep(500000);
	maestroSetSpeed(fd, 0);	
	
	
	one_button_pressed = 0;
	two_button_pressed = 0;

	gettimeofday(&tim, NULL);
	t_curr=tim.tv_sec+(tim.tv_usec/1000000.0);
	
	//Initialise PID parameters for the x-axis and the wait or DELTA_T/2
	//pid_params x = {KC, TAU_I, TAU_D, TAU_F, 0, 0, (x_cord+measuredx_dot*(t_curr-t_measuredx))/1000, 0, 0, 0, 0, 0};  //initialise PID parameters for x axis
	pid_params x = {KC, TAU_I, TAU_D, TAU_F, 0, 0, x_cord/1000, 0, 0, 0, 0, 0};
	gettimeofday(&tim, NULL);
	t_x_past=tim.tv_sec+(tim.tv_usec/1000000.0); //initialise t_x_past with current time (in seconds)


	wait_for_deltat(&tim, &t_x_curr, &t_x_past, &deltaT_x, DELTA_T/2); //Wait until Delta_T/2

	gettimeofday(&tim, NULL);
	t_curr=tim.tv_sec+(tim.tv_usec/1000000.0);
	// Initialise  PID parameters for the y-axis
	//pid_params y = {KC, TAU_I, TAU_D, TAU_F, 0, 0, (y_cord+measuredy_dot*(t_curr-t_measuredy))/1000, 0, 0, 0, 0, 0}; //initialise PID paramaters for y axis
	pid_params y = {KC, TAU_I, TAU_D, TAU_F, 0, 0, y_cord/1000, 0, 0, 0, 0, 0};
	gettimeofday(&tim, NULL);
	t_y_past=tim.tv_sec+(tim.tv_usec/1000000.0); //initialise t_y_past with current time (in seconds)
	double t_start = t_y_past;

	int pid_mode = 0;
	int new_cycles = 0;

	while(!next_mode)
	{
		if (one_button_pressed)
		{
			x.set_pt = 0;
			y.set_pt = 0;
			one_button_pressed = 0;
			pid_mode = 0;
		}

		if (two_button_pressed)
		{
			pid_mode = 1;
			new_cycles = 0;
			two_button_pressed = 0;
			x.set_pt = x_pos[pos_current];
			y.set_pt = y_pos[pos_current];
			printf("x_pos = %f, y_pos = %f\n", x.set_pt, y.set_pt);
			pos_current++;
			pos_current = pos_current%4;

		}

		if (pid_mode == 1)
		{
			new_cycles ++;
			if (new_cycles % 250 == 0)
			{
				x.set_pt = x_pos[pos_current];
				y.set_pt = y_pos[pos_current];
				printf("x_pos = %f, y_pos = %f\n", x.set_pt, y.set_pt);
				pos_current++;
				pos_current = pos_current%4;
			}
		}


		wait_for_deltat(&tim, &t_x_curr, &t_x_past, &deltaT_x, DELTA_T); //Wait until DELTA_T for x-axis
	

		x.pos_past = x.pos_curr;  //store past ball position
		x.pos_curr=(x_cord+measuredx_dot*(t_x_curr-t_measuredx)+0.5*measuredx_dot_dot*pow((t_x_curr-t_measuredx),2.0))/1000;  //Get balls position from 2nd order estimator
		x.u_D_past = x.u_D;  //store past derivative control signal
		x.u_act_past = x.u_act;  //store past control signal
		x.error = (x.set_pt - x.pos_curr); //calculate error r(t) - y(t)
		t_x_past = t_x_curr;  //Save new time

		if (x.error > 0.02 || x.error < -0.05)
                {
                        x.tauF = TAUF_FAST;
                        x.kc = KC_FAST;
                        x.tauD = TAUD_FAST;
                        x.tauI = TAUI_FAST;
                }
                else
                {
                        x.tauF = TAU_F;
                        x.kc = KC;
                        x.tauD = TAU_D;
                        x.tauI = TAU_I;
                }


		//Calculate new derivative term
		x.u_D = (x.tauF/(x.tauF+deltaT_x/1000))*x.u_D_past + ((x.kc*x.tauD)/(x.tauF+deltaT_x/1000))*(x.pos_curr - x.pos_past);
		//Caluclate new control signal
		x.u_act = x.u_act_past + x.kc*(-x.pos_curr + x.pos_past) + ((x.kc * deltaT_x/1000)/x.tauI)*(x.error) - x.u_D + x.u_D_past;


		if (x.u_act > UMAX) x.u_act = UMAX;
		if (x.u_act < UMIN) x.u_act = UMIN;


		du_x_past = du_x;
		du_x = (x.u_act - x.u_act_past)/deltaT_x;  //Calculate derivative of control signal

		if (du_x < DUMIN)
		{
			du_x = DUMIN;
			x.u_act = x.u_act_past + DUMIN*deltaT_x;
		}

		
		if (du_x > DUMAX)
		{
                        du_x = DUMAX;
                        x.u_act = x.u_act_past + DUMAX*deltaT_x;
                }


		ddu_x = (du_x - du_x_past)/deltaT_x;  //Calculate double derivative of control signal

		if (ddu_x < DDUMIN)
		{
			du_x = du_x_past + DDUMIN*deltaT_x;
			x.u_act = x.u_act_past + du_x*deltaT_x;
		}

                if (ddu_x  > DDUMAX)
                {
                        du_x = du_x_past + DDUMAX*deltaT_x;
                        x.u_act = x.u_act_past + du_x*deltaT_x;
                } 
    		

		//Output Control Signal
		target=(int)((x.u_act*(2.4*180/PI))*40+(4*X_SERVO_CENTRE));
		maestroSetTarget(fd, 0, target);
		//printf("Test Control Signal u_act_x = %f degrees\n", x.u_act*(180/PI));

		wait_for_deltat(&tim, &t_y_curr, &t_y_past, &deltaT_y, DELTA_T); //Get Accurate timings

		y.pos_past = y.pos_curr;  //store past ball position
		//y.pos_curr = (y_cord+measuredy_dot*(t_y_curr-t_measuredy))/1000;  //current ball position is equal to the coordinates read from the touchscreen
		y.pos_curr=(y_cord+measuredy_dot*(t_y_curr-t_measuredy)+0.5*measuredy_dot_dot*pow((t_y_curr-t_measuredy),2.0))/1000;
		y.u_D_past = y.u_D;  //store past derivative control signal
		y.u_act_past = y.u_act;
		y.error = (y.set_pt - y.pos_curr);
		t_y_past = t_y_curr;  //Save new time

                if (y.error > 0.02 || y.error < -0.05)
                {
                        y.tauF = TAUF_FAST;
                        y.kc = KC_FAST;
                        y.tauD = TAUD_FAST;
                        y.tauI = TAUI_FAST;
                }
                else
                {
                        y.tauF = TAU_F;
                        y.kc = KC;
                        y.tauD = TAU_D;
                        y.tauI = TAU_I;
                }

		//Calculate new derivative term
		y.u_D = (y.tauF/(y.tauF+deltaT_y/1000))*y.u_D_past + ((y.kc*y.tauD)/(y.tauF+deltaT_y/1000))*(y.pos_curr - y.pos_past);
		//Caluclate new control signal
		y.u_act = y.u_act_past + y.kc*(-y.pos_curr + y.pos_past) + ((y.kc * deltaT_y/1000)/y.tauI)*(y.error) - y.u_D + y.u_D_past;

		if (x.u_act > UMAX) x.u_act = UMAX;
		if (x.u_act < UMIN) x.u_act = UMIN;

                du_y_past = du_y;
                du_y = (y.u_act - y.u_act_past)/deltaT_y;  //Calculate derivative of control signal

                if (du_y < DUMIN)
                {
                        du_y = DUMIN;
                        y.u_act = y.u_act_past + DUMIN*deltaT_y;
                }


                if (du_y > DUMAX)
                {
                        du_y = DUMAX;
                        y.u_act = y.u_act_past + DUMAX*deltaT_y;
                }


                ddu_y = (du_y - du_y_past)/deltaT_y;  //Calculate double derivative of control signal

                if (ddu_y < DDUMIN)
                {
                        du_y = du_y_past + DDUMIN*deltaT_y;
                        y.u_act = y.u_act_past + du_y*deltaT_y;
                }

                if (ddu_y  > DDUMAX)
                {
                        du_y = du_y_past + DDUMAX*deltaT_y;
                        y.u_act = y.u_act_past + du_y*deltaT_y;
                }
		

		//Output control signal
		target=(int)(y.u_act*(2.4*180/PI)*40+(4*Y_SERVO_CENTRE));
		maestroSetTarget(fd, 1, target);
		//printf("Test Control Signal u_act_y = %f degrees\n", y.u_act*(180/PI));

	}

	close_maestro(fd);
	return;
}



void wait_for_deltat(struct timeval *tim, double *t_curr, double *t_past, double *new_delta, double required_delta)
{
		gettimeofday(tim, NULL);
		(*t_curr)=(tim->tv_sec)+(tim->tv_usec/1000000.0); 
		(*new_delta) = (*t_curr-*t_past)*1000;

		struct timespec t_curr_spec, t_delta_spec;
		clock_gettime(CLOCK_MONOTONIC, &t_curr_spec);
		//printf("current ns= %ld\n", t_curr_spec.tv_nsec);

		long int new_delta_long = required_delta*1000000-*new_delta*1000000;

		long int new_delta_long_secs = t_curr_spec.tv_sec + ((double)t_curr_spec.tv_nsec/1000000000 + (double)new_delta_long/1000000000);
		new_delta_long = (t_curr_spec.tv_nsec+new_delta_long)%1000000000;

		t_delta_spec.tv_sec =new_delta_long_secs;
		t_delta_spec.tv_nsec = new_delta_long;

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t_delta_spec, NULL);


		gettimeofday(tim, NULL);
		(*t_curr)=tim->tv_sec+(tim->tv_usec/1000000.0); 
		(*new_delta) = (*t_curr-*t_past)*1000;


}
