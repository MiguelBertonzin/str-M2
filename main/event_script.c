#include "event_script.h"

// Uma execucao por boot: a emergencia fica travada ate reiniciar a placa.
// O roteiro nao depende do tempo de leitura do operador ou da task de guia.
// A repeticao do mesmo canal respeita o debounce de 1,2 s da M1.
const scripted_event_t EVENT_SCRIPT[EVENT_SCRIPT_COUNT] = {
    { 2000000ULL, INPUT_C_MASK},
    { 4000000ULL, INPUT_B_MASK},
    { 6000000ULL, INPUT_C_MASK},
    { 8000000ULL, INPUT_A_MASK},
    {10000000ULL, INPUT_B_MASK},
    {12000000ULL, INPUT_C_MASK},
    {14000000ULL, INPUT_B_MASK},
    {16000000ULL, INPUT_C_MASK},
    {18000000ULL, INPUT_B_MASK},
    {20000000ULL, INPUT_C_MASK},
    {22000000ULL, INPUT_B_MASK | INPUT_C_MASK},
    {24000000ULL, INPUT_D_MASK},
    {26000000ULL, INPUT_C_MASK},
};
