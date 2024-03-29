/**
 * @file
 *
 * \brief A hospital pediatric urgency with a Manchester triage system.
 */

#include  <stdio.h>
#include  <stdlib.h>
#include  <string.h>
#include  <libgen.h>
#include  <unistd.h>
#include  <sys/wait.h>
#include  <sys/types.h>
#include  <thread.h>
#include  <math.h>
#include  <stdint.h>
#include  <signal.h>
#include  <utils.h>
#include  "settings.h"
#include  "pfifo.h"

#include "thread.h"

#include <iostream>

#define USAGE "Synopsis: %s [options]\n" \
   "\t----------+-------------------------------------------\n" \
   "\t  Option  |          Description                      \n" \
   "\t----------+-------------------------------------------\n" \
   "\t -p num   | number of patients (dfl: 4)               \n" \
   "\t -n num   | number of nurses (dfl: 1)                 \n" \
   "\t -d num   | number of doctors (dfl: 1)                \n" \
   "\t -h       | this help                                 \n" \
   "\t----------+-------------------------------------------\n"

/**
 * \brief Patient data structure
 */
typedef struct
{
   char name[MAX_NAME+1];
   int done; // 0: waiting for consultation; 1: consultation finished
} Patient;

typedef struct
{
    int num_patients;
    Patient all_patients[MAX_PATIENTS];
    PriorityFIFO triage_queue;
    PriorityFIFO doctor_queue;
} HospitalData;

HospitalData * hd = NULL;

static pthread_t* patientsThr;
static pthread_t* nursesThr;
static pthread_t* doctorsThr;
static pthread_mutex_t* patientsMtx;
static pthread_cond_t* patientsDone;

static pthread_mutex_t endMutex;
static int didEnd = 0;

/**
 *  \brief patient verification test
 */
#define check_valid_patient(id) do { check_valid_id(id); check_valid_name(hd->all_patients[id].name); } while(0)

int random_manchester_triage_priority();
void new_patient(Patient* patient); // initializes a new patient
void random_wait();

/* ************************************************* */

/* changes may be required to this function */
void init_simulation(uint32_t np)
{
   printf("Initializing simulation\n");
   hd = (HospitalData*)mem_alloc(sizeof(HospitalData)); // mem_alloc is a malloc with NULL pointer verification
   memset(hd, 0, sizeof(HospitalData));
   hd->num_patients = np;
   init_pfifo(&hd->triage_queue);
   init_pfifo(&hd->doctor_queue);
}

/* ************************************************* */

void* nurse_life(void* argv)
{
	while(true) 
	{
	   printf("\e[34;01mNurse: get next patient\e[0m\n");
	   uint32_t patient = retrieve_pfifo(&hd->triage_queue);

	   mutex_lock(&endMutex);
   	   if(didEnd) break;
       mutex_unlock(&endMutex);
	   
	   check_valid_patient(patient);
	   printf("\e[34;01mNurse: evaluate patient %u priority\e[0m\n", patient);
	   uint32_t priority = random_manchester_triage_priority();
	   printf("\e[34;01mNurse: add patient %u with priority %u to doctor queue\e[0m\n", patient, priority);
	   insert_pfifo(&hd->doctor_queue, patient, priority);
	}
}

/* ************************************************* */

void* doctor_life(void* argv)
{
	while(true)
	{
	   printf("\e[32;01mDoctor: get next patient\e[0m\n");
	   uint32_t patient = retrieve_pfifo(&hd->doctor_queue);

       mutex_lock(&endMutex);
	   if(didEnd) break;
       mutex_unlock(&endMutex);

	   mutex_lock(&patientsMtx[patient]);
	   
	   check_valid_patient(patient);
	   printf("\e[32;01mDoctor: treat patient %u\e[0m\n", patient);
	   random_wait();
	   printf("\e[32;01mDoctor: patient %u treated\e[0m\n", patient);
	   hd->all_patients[patient].done = 1;
	   
       cond_broadcast(&patientsDone[patient]);

	   mutex_unlock(&patientsMtx[patient]);
	}
}

/* ************************************************* */

void patient_goto_urgency(int id)
{
   new_patient(&hd->all_patients[id]);
   check_valid_name(hd->all_patients[id].name);
   printf("\e[30;01mPatient %s (number %u): get to hospital\e[0m\n", hd->all_patients[id].name, id);
   insert_pfifo(&hd->triage_queue, id, 1); // all elements in triage queue with the same priority!
}

void patient_wait_end_of_consultation(int id)
{
   mutex_lock(&patientsMtx[id]);
   
   while(!hd->all_patients[id].done) 
   {
   		cond_wait(&patientsDone[id], &patientsMtx[id]);
   }
  
   check_valid_name(hd->all_patients[id].name);
   printf("\e[30;01mPatient %s (number %u): health problems treated\e[0m\n", hd->all_patients[id].name, id);

   mutex_unlock(&patientsMtx[id]);
}

void* patient_life(void* argv)
{
   int id = *(int*)argv;

   free(argv);

   patient_goto_urgency(id);
   patient_wait_end_of_consultation(id);
   
   memset(&(hd->all_patients[id]), 0, sizeof(Patient)); // patient finished

   return NULL;
}

/* ************************************************* */

int main(int argc, char *argv[])
{
   uint32_t npatients = 4;  ///< number of patients
   uint32_t nnurses = 1;    ///< number of triage nurses
   uint32_t ndoctors = 1;   ///< number of doctors

   /* command line processing */
   int option;
   while ((option = getopt(argc, argv, "p:n:d:h")) != -1)
   {
      switch (option)
      {
         case 'p':
            npatients = atoi(optarg);
            if (npatients < 1 || npatients > MAX_PATIENTS)
            {
               fprintf(stderr, "Invalid number of patients!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'n':
            nnurses = atoi(optarg);
            if (nnurses < 1)
            {
               fprintf(stderr, "Invalid number of nurses!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'd':
            ndoctors = atoi(optarg);
            if (ndoctors < 1)
            {
               fprintf(stderr, "Invalid number of doctors!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'h':
            printf(USAGE, basename(argv[0]));
            return EXIT_SUCCESS;
         default:
            fprintf(stderr, "Non valid option!\n");
            fprintf(stderr, USAGE, basename(argv[0]));
            return EXIT_FAILURE;
      }
   }

   /* start random generator */
   srand(getpid());

   init_simulation(npatients);

   endMutex = PTHREAD_MUTEX_INITIALIZER;
   patientsThr = new pthread_t[npatients] {};
   nursesThr = new pthread_t[nnurses] {};
   doctorsThr = new pthread_t[ndoctors] {};
   patientsMtx = new pthread_mutex_t[npatients] {};
   patientsDone = new pthread_cond_t[npatients] {};

   for(int i = 0; i < nnurses; i++) 
   {
     	pthread_create(&nursesThr[i], NULL, nurse_life, NULL);
   }

   for(int i = 0; i < ndoctors; i++) 
   {
       	pthread_create(&doctorsThr[i], NULL, doctor_life, NULL);
   }

   for(int i = 0; i < npatients; i++) 
   {
   		patientsMtx[i] = PTHREAD_MUTEX_INITIALIZER;
   		patientsDone[i] = PTHREAD_COND_INITIALIZER;

		int* id = (int*) malloc(sizeof(int)); 
		*id = i;
		
   		pthread_create(&patientsThr[i], NULL, patient_life, (void*) id);
   }

   for(int i = 0; i < npatients; i++)
   {
   		pthread_join(patientsThr[i], NULL);
   }

   mutex_lock(&endMutex);
   didEnd = true;
   mutex_unlock(&endMutex);
   
   return EXIT_SUCCESS;
}


/* YOU MAY IGNORE THE FOLLOWING CODE */

int random_manchester_triage_priority()
{
   int result;
   int perc = (int)(100*(double)rand()/((double)RAND_MAX)); // in [0;100]
   if (perc < 10)
      result = RED;
   else if (perc < 30)
      result = ORANGE;
   else if (perc < 50)
      result = YELLOW;
   else if (perc < 75)
      result = GREEN;
   else
      result = BLUE;
   return result;
}

static char **names = (char *[]) {"Ana", "Miguel", "Luis", "Joao", "Artur", "Maria", "Luisa", "Mario", "Augusto", "Antonio", "Jose", "Alice", "Almerindo", "Gustavo", "Paulo", "Paula", NULL};

char* random_name()
{
   static int names_len = 0;
   if (names_len == 0)
   {
      for(names_len = 0; names[names_len] != NULL; names_len++)
         ;
   }

   return names[(int)(names_len*(double)rand()/((double)RAND_MAX+1))];
}

void new_patient(Patient* patient)
{
   strcpy(patient->name, random_name());
   patient->done = 0;
}

void random_wait()
{
   usleep((useconds_t)(MAX_WAIT*(double)rand()/(double)RAND_MAX));
}

