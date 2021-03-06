#ifndef TEMP_H_KNWLM1TX
#define TEMP_H_KNWLM1TX

#include <stdio.h>

#include "util/bitarray.h"
#include "util/bool.h"
#include "util/list.h"
#include "util/util.h"

#include "core/symbol.h"

typedef struct Temp_temp_ * Temp_temp;
Temp_temp Temp_NewTemp (void);

LIST_DEFINE(Temp_tempList, Temp_temp)
LIST_CONST_DEFINE(Temp_TempList, Temp_tempList, Temp_temp)

bool Temp_IsTempInList (Temp_tempList list, Temp_temp temp);

typedef S_symbol Temp_label;
Temp_label Temp_NewLabel (void);
Temp_label Temp_NamedLabel (const char * s);
const char * Temp_LabelString (Temp_label s);

LIST_DEFINE(Temp_labelList, Temp_label)
LIST_CONST_DEFINE(Temp_LabelList, Temp_labelList, Temp_label)

typedef struct Temp_map_ * Temp_map;
Temp_map Temp_Empty (void);
Temp_map Temp_LayerMap (Temp_map over, Temp_map under);
void Temp_Enter (Temp_map m, Temp_temp t, const char * s);
const char * Temp_Look (Temp_map m, Temp_temp t);
void Temp_DumpMap (FILE * out, Temp_map m);

Temp_map Temp_Name (void);

Temp_temp GetTempFromList (Temp_tempList list, int index);

int Temp_GetTempIndex (Temp_temp temp);
Temp_tempList Temp_SortTempList (Temp_tempList list);

/**
 * Maps a Temp_tempList subset to bit array of arbitrary length. The result
 * is char * type with the length calculated as:
 *
 *       size / 8 + (size % 8 ? 1 : 0)
 *
 * thus, the result bit array size will be 1 byte minimum
 *
 *
 * @size Number of temporaries in the set
 * @subset A subset to be mapped
 * @set A set to be mapped against
 */
// TODO: Artyom Goncharov remove length
// TODO: Artyom Goncharov make it accept an instance of BitArray
BitArray MapTempList (int length, Temp_tempList subset, Temp_tempList set);

/**
 * Reverse procedure to MapTempList. The order of the result list is not related
 * to the main temp set.
 *
 * @size Number of temporaries in the set
 * @map A bit array map produced by MapTempList
 * @set A set to be mapped against
 */
Temp_tempList UnmapTempList (BitArray ba, Temp_tempList set);

#endif /* end of include guard: TEMP_H_KNWLM1TX */
