/*
 * disease.c


 *
 *  Created on: 18 Mar 2020
 *      Author: hinchr
 */

#include "model.h"
#include "individual.h"
#include "utilities.h"
#include "constant.h"
#include "params.h"
#include "network.h"
#include "disease.h"
#include "structure.h"
#include "interventions.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/*****************************************************************************************
*  Name:		set_up_transition_times
*  Description: sets up discrete distributions for the times it takes to
*  				transition along edges of the disease model
*
*  Returns:		void
******************************************************************************************/
void set_up_transition_times( model *model )
{
	parameters *params = model->params;
	int idx;
	int **transitions;

	model->transition_time_distributions = calloc( N_TRANSITION_TYPES, sizeof( int*) );
	for( idx = 0; idx < N_TRANSITION_TYPES; idx++ )
		model->transition_time_distributions[idx] = calloc( N_DRAW_LIST, sizeof( int ) );
	transitions = model->transition_time_distributions;

	gamma_draw_list( transitions[ASYMPTOMATIC_RECOVERED], 	   N_DRAW_LIST, params->mean_asymptomatic_to_recovery, params->sd_asymptomatic_to_recovery );
	gamma_draw_list( transitions[PRESYMPTOMATIC_SYMPTOMATIC],  N_DRAW_LIST, params->mean_time_to_symptoms,         params->sd_time_to_symptoms );
	gamma_draw_list( transitions[PRESYMPTOMATIC_MILD_SYMPTOMATIC_MILD], N_DRAW_LIST, params->mean_time_to_symptoms,params->sd_time_to_symptoms );
	gamma_draw_list( transitions[SYMPTOMATIC_RECOVERED],   	   N_DRAW_LIST, params->mean_time_to_recover,  		   params->sd_time_to_recover );
	gamma_draw_list( transitions[SYMPTOMATIC_MILD_RECOVERED],  N_DRAW_LIST, params->mean_time_to_recover,  		   params->sd_time_to_recover );
	gamma_draw_list( transitions[HOSPITALISED_RECOVERED],      N_DRAW_LIST, params->mean_time_hospitalised_recovery, params->sd_time_hospitalised_recovery);
	gamma_draw_list( transitions[CRITICAL_HOSPITALISED_RECOVERING], N_DRAW_LIST, params->mean_time_critical_survive, params->sd_time_critical_survive);
	gamma_draw_list( transitions[CRITICAL_DEATH],              N_DRAW_LIST, params->mean_time_to_death,    		     params->sd_time_to_death );
	gamma_draw_list( transitions[HOSPITALISED_RECOVERING_RECOVERED], N_DRAW_LIST, params->mean_time_hospitalised_recovery, params->sd_time_hospitalised_recovery);
	bernoulli_draw_list( transitions[SYMPTOMATIC_HOSPITALISED],N_DRAW_LIST, params->mean_time_to_hospital );
	gamma_draw_list( transitions[HOSPITALISED_CRITICAL],   N_DRAW_LIST, params->mean_time_to_critical, params->sd_time_to_critical );
	shifted_geometric_draw_list( transitions[RECOVERED_SUSCEPTIBLE], N_DRAW_LIST, params->mean_time_to_susceptible_after_shift, params->time_to_susceptible_shift );

}

/*****************************************************************************************
*  Name:		estimate_mean_interactions_by_age
*  Description: estimates the weighted mean number of interactions by age
*  				each interaction is weighted by the type factor
*
* 	Argument:  age - integer of the age group, if age == -1 then include everyone
*
*  Returns:		void
******************************************************************************************/
double estimate_mean_interactions_by_age( model *model, int age )
{
	long pdx, ndx;
	long people = 0;
	double inter  = 0;
	double *weight = model->params->relative_transmission;

	for( pdx = 0; pdx < model->params->n_total; pdx++ )
		if( ( model->population[pdx].age_type == age ) | ( age == -1 ) )
		{
			people++;
			inter += model->population[pdx].base_random_interactions * weight[RANDOM];
		}

	for( pdx = 0; pdx < model->household_network->n_edges; pdx++ )
	{
		if( ( model->population[model->household_network->edges[pdx].id1].age_type == age ) | ( age == -1 ) )
			inter += weight[HOUSEHOLD];
		if( ( model->population[model->household_network->edges[pdx].id2].age_type == age ) | ( age == -1 ))
			inter += weight[HOUSEHOLD];
	}

	for( ndx = 0; ndx < model->n_occupation_networks ; ndx++ )
		for( pdx = 0; pdx < model->occupation_network[ndx]->n_edges; pdx++ )
		{
			if( ( model->population[model->occupation_network[ndx]->edges[pdx].id1].age_type == age ) | ( age == -1 ))
				inter += model->params->daily_fraction_work * weight[OCCUPATION];
			if( ( model->population[model->occupation_network[ndx]->edges[pdx].id2].age_type == age  ) | ( age == -1 ))
				inter  += model->params->daily_fraction_work * weight[OCCUPATION];
		}

	return 1.0 * inter / people;
}

/*****************************************************************************************
*  Name:		set_up_infectious curves
*  Description: sets up discrete distributions and functions which are used to
*  				model events and calculates infectious rate per interaction.
*
*				If relative susceptibility by age is a per interaction measure, then
*  				the infectious rate is adjusted by the total mean interactions.
*
*				If relative susceptibility by age is a per day measure, then
*				the infectious rate per interaction adult is the infectious rate divided
*  				by the mean number of daily interactions of an adult. Adjustments are calculated
*  				for children and elderly based upon their difference in the number of daily
*  				interactions and the relative overall susceptibility.
*
*  Returns:		void
******************************************************************************************/
void set_up_infectious_curves( model *model )
{
	parameters *params = model->params;
	double infectious_rate, type_factor;
	int type, group;

	infectious_rate = params->infectious_rate;
	if( params->relative_susceptibility_by_interaction )
	{
		if( model->mean_interactions > 0 )
			infectious_rate /= model->mean_interactions;
		for( group = 0; group < N_AGE_GROUPS; group++ )
			params->adjusted_susceptibility[group] = params->relative_susceptibility[group];
	}
	else
	{
		infectious_rate /= model->mean_interactions_by_age[AGE_TYPE_ADULT];

		for( group = 0; group < N_AGE_GROUPS; group++ )
			params->adjusted_susceptibility[group] = params->relative_susceptibility[group] * model->mean_interactions_by_age[AGE_TYPE_ADULT] / model->mean_interactions_by_age[AGE_TYPE_MAP[group]];
	}

	for( type = 0; type < N_INTERACTION_TYPES; type++ )
	{
		type_factor = params->relative_transmission_used[type];

		gamma_rate_curve( model->event_lists[PRESYMPTOMATIC].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor );

		gamma_rate_curve( model->event_lists[PRESYMPTOMATIC_MILD].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor * params->mild_infectious_factor  );

		gamma_rate_curve( model->event_lists[ASYMPTOMATIC].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor * params->asymptomatic_infectious_factor);

		gamma_rate_curve( model->event_lists[SYMPTOMATIC].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor );

		gamma_rate_curve( model->event_lists[SYMPTOMATIC_MILD].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor * params->mild_infectious_factor );

		gamma_rate_curve( model->event_lists[HOSPITALISED].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor );

		gamma_rate_curve( model->event_lists[HOSPITALISED_RECOVERING].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
							  params->sd_infectious_period, infectious_rate * type_factor );

		gamma_rate_curve( model->event_lists[CRITICAL].infectious_curve[type], MAX_INFECTIOUS_PERIOD, params->mean_infectious_period,
						  params->sd_infectious_period, infectious_rate * type_factor );
	};
}
/*****************************************************************************************
*  Name:		transmit_virus_by_type
*  Description: Transmits virus over the interaction network for a type of
*  				infected people
*  Returns:		void
******************************************************************************************/
void transmit_virus_by_type(
	model *model,
	int type
)
{
	long idx, jdx, n_infected;
	int day, n_interaction, t_infect, strain_idx;
	double hazard_rate, infector_mult;
	event_list *list = &(model->event_lists[type]);
	event *event, *next_event;
	interaction *interaction;
	individual *infector;

	for( day = model->time-1; day >= max( 0, model->time - MAX_INFECTIOUS_PERIOD ); day-- )
	{
		n_infected  = list->n_daily_current[ day];
		next_event  = list->events[ day ];

		for( idx = 0; idx < n_infected; idx++ )
		{
			event      = next_event;
			next_event = event->next;
			infector   = event->individual;

			t_infect = model->time - time_infected_infection_event( infector->infection_events );
			if( t_infect >= MAX_INFECTIOUS_PERIOD )
				continue;

			n_interaction = infector->n_interactions[ model->interaction_day_idx ];
			if( n_interaction > 0 )
			{
				interaction   = infector->interactions[ model->interaction_day_idx ];
				infector_mult = infector->infectiousness_multiplier * infector->infection_events->strain_multiplier;
				strain_idx 	  = infector->infection_events->strain_idx;

				for( jdx = 0; jdx < n_interaction; jdx++ )
				{
					// if( interaction->individual->status == SUSCEPTIBLE )
					if( ( interaction->individual->status == SUSCEPTIBLE ) && ( interaction->individual->time_susceptible[strain_idx] <= day ) )
					{
						hazard_rate   = list->infectious_curve[interaction->type][ t_infect - 1 ] * infector_mult;
                        interaction->individual->hazard[strain_idx] -= hazard_rate;
						if( interaction->individual->hazard[strain_idx] < 0 )
						{
							new_infection( model, interaction->individual, infector, interaction->network_id );
							interaction->individual->infection_events->infector_network = interaction->type;
						}
					}
					interaction = interaction->next;
				}
			}
		}
	}
}

/*****************************************************************************************
*  Name:		transmit_virus
*  Description: Transmits virus over the interaction network
*
*  				Transmission by groups depending upon disease status.
*  				Note that quarantine is not a disease status and they will either
*  				be presymptomatic/symptomatic/asymptomatic and quarantining is
*  				modelled by reducing the number of interactions in the network.
*
*  Returns:		void
******************************************************************************************/
void transmit_virus( model *model )
{
	transmit_virus_by_type( model, PRESYMPTOMATIC );
	transmit_virus_by_type( model, PRESYMPTOMATIC_MILD );
	transmit_virus_by_type( model, SYMPTOMATIC );
	transmit_virus_by_type( model, SYMPTOMATIC_MILD );
	transmit_virus_by_type( model, ASYMPTOMATIC );
	transmit_virus_by_type( model, HOSPITALISED );
	transmit_virus_by_type( model, CRITICAL );
	transmit_virus_by_type( model, HOSPITALISED_RECOVERING );

}

/*****************************************************************************************
*  Name:		seed_infect_by_idx
*  Description: infects a new individual from an external source
*  Returns:		void
******************************************************************************************/
short seed_infect_by_idx(
	model *model,
	long pdx,
	float strain_multiplier,
	int strain_idx,
	int network_id
)
{
	individual *infected = &(model->population[ pdx ]);

	if( infected->status != SUSCEPTIBLE )
		return FALSE;

	infected->infection_events->strain_multiplier = strain_multiplier;
	infected->infection_events->strain_idx = strain_idx;
	new_infection( model, infected, infected, network_id );
	return TRUE;
}

/*****************************************************************************************
*  Name:		new_infection
*  Description: infects a new individual from another individual
*  Returns:		void
******************************************************************************************/
void new_infection(
	model *model,
	individual *infected,
	individual *infector,
	int network_id
)
{
	double draw       = gsl_rng_uniform( rng );
	double asymp_frac = model->params->fraction_asymptomatic[infected->age_group];
	double mild_frac  = model->params->mild_fraction[infected->age_group];

	if( vaccine_protected( infected ) )
		asymp_frac = 1;

	infected->infection_events->infector = infector;
	infected->infection_events->infector_status = infector->status;
	infected->infection_events->infector_hospital_state = infector->hospital_state;
	infected->infection_events->network_id = network_id;
	infected->infection_events->strain_multiplier = infector->infection_events->strain_multiplier;
	infected->infection_events->strain_idx = infector->infection_events->strain_idx;

	if( draw < asymp_frac )
	{
		transition_one_disese_event( model, infected, SUSCEPTIBLE, ASYMPTOMATIC, NO_EDGE );
		transition_one_disese_event( model, infected, ASYMPTOMATIC, RECOVERED, ASYMPTOMATIC_RECOVERED );
	}
	else if( draw < asymp_frac + mild_frac )
	{
		transition_one_disese_event( model, infected, SUSCEPTIBLE, PRESYMPTOMATIC_MILD, NO_EDGE );
		transition_one_disese_event( model, infected, PRESYMPTOMATIC_MILD, SYMPTOMATIC_MILD, PRESYMPTOMATIC_MILD_SYMPTOMATIC_MILD );
	}
	else
	{
		transition_one_disese_event( model, infected, SUSCEPTIBLE, PRESYMPTOMATIC, NO_EDGE );
		transition_one_disese_event( model, infected, PRESYMPTOMATIC, SYMPTOMATIC, PRESYMPTOMATIC_SYMPTOMATIC );
	}
	infected->infection_events->time_infected_infector =
		time_infected_infection_event(infector->infection_events);
}

/*****************************************************************************************
*  Name:		transition_one_disease_event
*  Description: Generic function for updating an individual with their next
*				event and adding the applicable events
*  Returns:		void
******************************************************************************************/
void transition_one_disese_event(
	model *model,
	individual *indiv,
	int from,
	int to,
	int edge
)
{
	indiv->status           = from;

	if( (from != NO_EVENT) | (from != SUSCEPTIBLE))
		indiv->infection_events->times[from] = model->time;
	if( indiv->current_disease_event != NULL )
		remove_event_from_event_list( model, indiv->current_disease_event );
	if( indiv->next_disease_event != NULL )
		indiv->current_disease_event = indiv->next_disease_event;

	if( indiv->quarantined == TRUE){
		if(from == SUSCEPTIBLE){
			model->n_quarantine_infected++;
			if(indiv->app_user == TRUE){
				model->n_quarantine_app_user_infected++;
			}
		}
	}

	if( to != NO_EVENT )
	{
		// indiv->infection_events->times[to]     = model->time + ifelse( ( edge == NO_EDGE ), 0, sample_transition_time( model, edge ) );
		indiv->infection_events->times[to]     = model->time + ifelse( ( edge == NO_EDGE ) || ( edge == RECOVERED_SUSCEPTIBLE ), 0, sample_transition_time( model, edge ) ); // instantaneously go from recovered to susceptible
		indiv->next_disease_event = add_individual_to_event_list( model, to, indiv, indiv->infection_events->times[to] );
	}
}

/*****************************************************************************************
*  Name:		transition_to_symptomatic
*  Description: Transitions infected who are due to become symptomatic. At this point
*  				there are 2 choices to be made:
*
*  				1. Will the individual require hospital treatment or will
*  				they recover without needing treatment
*  				2. Does the individual self-quarantine at this point and
*  				asks for a test
*
*  Returns:		void
******************************************************************************************/
void transition_to_symptomatic( model *model, individual *indiv )
{
	if( gsl_ran_bernoulli( rng, model->params->hospitalised_fraction[ indiv->age_group ] ) )
		transition_one_disese_event( model, indiv, SYMPTOMATIC, HOSPITALISED, SYMPTOMATIC_HOSPITALISED );
	else
		transition_one_disese_event( model, indiv, SYMPTOMATIC, RECOVERED, SYMPTOMATIC_RECOVERED );

	intervention_on_symptoms( model, indiv );
}

/*****************************************************************************************
*  Name:		transition_to_symptomatic_mild
*  Description: Transitions infected who are due to become symptomatic with mild symptoms.
*
*  				1. Add the recovery event
*  				2. Does the individual self-quarantine at this point and
*  				asks for a test
*
*  Returns:		void
******************************************************************************************/
void transition_to_symptomatic_mild( model *model, individual *indiv )
{
	transition_one_disese_event( model, indiv, SYMPTOMATIC_MILD, RECOVERED, SYMPTOMATIC_MILD_RECOVERED );
	intervention_on_symptoms( model, indiv );
}

/*****************************************************************************************
*  Name:		transition_to_hospitalised
*  Description: Transitions symptomatic individual to a severe disease state, and assigns them
*               to a hospital for upcoming hospital states.
*  Returns:		void
******************************************************************************************/
void transition_to_hospitalised( model *model, individual *indiv )
{
	set_hospitalised( indiv, model->params, model->time );


	if( model->params->hospital_on )
	{
		int assigned_hospital_idx = find_least_full_hospital( model, COVID_GENERAL );
		add_patient_to_waiting_list( indiv, &(model->hospitals[assigned_hospital_idx]), COVID_GENERAL );
	}
	else
	{
		model->event_lists[TRANSITION_TO_HOSPITAL].n_daily_current[model->time]+=1;
		model->event_lists[TRANSITION_TO_HOSPITAL].n_total+=1;

		if( gsl_ran_bernoulli( rng, model->params->critical_fraction[ indiv->age_group ] ) )
		{
			if( gsl_ran_bernoulli( rng, model->params->location_death_icu[ indiv->age_group ] ) )
				transition_one_disese_event( model, indiv, HOSPITALISED, CRITICAL, HOSPITALISED_CRITICAL );
			else
				transition_one_disese_event( model, indiv, HOSPITALISED, DEATH, HOSPITALISED_CRITICAL );
			}
		else
			transition_one_disese_event( model, indiv, HOSPITALISED, RECOVERED, HOSPITALISED_RECOVERED );
	}

	if( indiv->quarantined )
		intervention_quarantine_release( model, indiv );

	intervention_on_hospitalised( model, indiv );
}

/*****************************************************************************************
*  Name:		transition_to_critical
*  Description: Transitions hopsitalised individual to critical care
*  Returns:		void
******************************************************************************************/
void transition_to_critical( model *model, individual *indiv )
{
	set_critical( indiv, model->params, model->time );


	if( model->params->hospital_on )
	{
		remove_if_in_waiting_list(indiv, &model->hospitals[indiv->hospital_idx]);
		add_patient_to_waiting_list( indiv, &(model->hospitals[indiv->hospital_idx]), COVID_ICU );
	}
	else
	{
		model->event_lists[TRANSITION_TO_CRITICAL].n_daily_current[model->time]+=1;
		model->event_lists[TRANSITION_TO_CRITICAL].n_total+=1;

		if( gsl_ran_bernoulli( rng, model->params->fatality_fraction[ indiv->age_group ] ) )
			transition_one_disese_event( model, indiv, CRITICAL, DEATH, CRITICAL_DEATH );
		else
			transition_one_disese_event( model, indiv, CRITICAL, HOSPITALISED_RECOVERING, CRITICAL_HOSPITALISED_RECOVERING );
	}

	intervention_on_critical( model, indiv );

}

/*****************************************************************************************
*  Name:		transition_to_hospitalised_recovering
*  Description: Transitions out of critical those who survive
*  Returns:		void
******************************************************************************************/
void transition_to_hospitalised_recovering( model *model, individual *indiv )
{
	if( model->params->hospital_on )
	{
		remove_if_in_waiting_list( indiv, &model->hospitals[indiv->hospital_idx] );
		if( indiv->ward_type != COVID_GENERAL )
			add_patient_to_waiting_list( indiv, &model->hospitals[indiv->hospital_idx], COVID_GENERAL );
	}

	transition_one_disese_event( model, indiv, HOSPITALISED_RECOVERING, RECOVERED, HOSPITALISED_RECOVERING_RECOVERED );
	set_hospitalised_recovering( indiv, model->params, model->time );
}

/*****************************************************************************************
*  Name:		transition_to_recovered
*  Description: Transitions hospitalised and asymptomatic to recovered
*  Returns:		void
******************************************************************************************/
void transition_to_recovered( model *model, individual *indiv )
{
	if( model->params->hospital_on )
	{
		if( indiv->hospital_state != NOT_IN_HOSPITAL )
		{
			remove_if_in_waiting_list( indiv, &model->hospitals[indiv->hospital_idx] );
			transition_one_hospital_event( model, indiv, indiv->hospital_state, DISCHARGED, NO_EDGE );
		}
	}

	transition_one_disese_event( model, indiv, RECOVERED, SUSCEPTIBLE, RECOVERED_SUSCEPTIBLE );
	set_recovered( indiv, model->params, model->time, model);
}

/*****************************************************************************************
*  Name:               transition_to_susceptible
*  Description: Transitions recovered to susceptible
*  Returns:            void
******************************************************************************************/
void transition_to_susceptible( model *model, individual *indiv )
{
       transition_one_disese_event( model, indiv, SUSCEPTIBLE, NO_EVENT, NO_EDGE );
       set_susceptible( indiv, model->params, model->time, model );
}


/*****************************************************************************************
*  Name:		transition_to_death
*  Description: Transitions hospitalised to death
*  Returns:		void
******************************************************************************************/
void transition_to_death( model *model, individual *indiv )
{
	if( model->params->hospital_on )
	{
		if( !not_in_hospital(indiv) )
		{
			remove_if_in_waiting_list( indiv, &model->hospitals[indiv->hospital_idx] );
			transition_one_hospital_event( model, indiv, indiv->hospital_state, MORTUARY, NO_EDGE );
		}
	}

	transition_one_disese_event( model, indiv, DEATH, NO_EVENT, NO_EDGE );
	set_dead( indiv, model->params, model->time );
}

/*****************************************************************************************
*  Name:		n_newly_infected
*  Description: Calculates the total nubmber of newly infected people on a day
*  Returns:		long
******************************************************************************************/
long n_newly_infected( model *model, int time )
{
	if( time > model->time )
		return ERROR;

	int idx;
	long total = 0;

	for( idx = 0; idx < N_NEWLY_INFECTED_STATES; idx++ )
		total += n_total_by_day( model, NEWLY_INFECTED_STATES[ idx], time );

	return total;
}

/*****************************************************************************************
*  Name:		calculate_R_instanteous
*  Description: Calculates the instantaneous value of R using time-series of known infections
*  				and the known generation time distribution
*
*  				percentile is the value of the posterior distribution e.g. 0.5 is the median
*
*  Returns:		double
******************************************************************************************/
double calculate_R_instanteous( model *model, int time, double percentile )
{
	if( ( time > model->time ) || ( percentile <= 0 ) || ( percentile >= 1 ) )
		return ERROR;

	int day;
	double expected_infections = 0;
	long actual_infections     = n_newly_infected( model, time );
	double *generation_dist    = calloc( MAX_INFECTIOUS_PERIOD, sizeof(double) );

	gamma_rate_curve( generation_dist, MAX_INFECTIOUS_PERIOD, model->params->mean_infectious_period, model->params->sd_infectious_period, 1);

	day = time-1;
	while( ( day >= 0 ) & ( ( time - day ) < MAX_INFECTIOUS_PERIOD ) )
	{
		expected_infections += generation_dist[ time - day - 1 ] * n_newly_infected( model, day );
		day--;
	}

	if( ( actual_infections <= 1 ) || ( expected_infections <= 1 ) )
		return ERROR;

	return inv_incomplete_gamma_p( percentile, actual_infections ) / expected_infections;
}



