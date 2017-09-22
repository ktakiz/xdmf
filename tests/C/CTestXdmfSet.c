#include "XdmfArray.hpp"
#include "XdmfArrayType.hpp"
#include "XdmfWriter.hpp"
#include "XdmfHDF5Writer.hpp"
#include "XdmfReader.hpp"
#include "XdmfSet.hpp"
#include "XdmfSetType.hpp"

#include "stdio.h"
#include "stdlib.h"

#include "string.h"
#include "assert.h"

int main()
{

  XDMFSET * set = XdmfSetNew();

  int i = 0;
  int status = 0;
  for (i = 0; i < 10; i++) {
    XdmfSetPushBack(set, &i, XDMF_ARRAY_TYPE_INT32, &status);
  }
  XdmfSetSetType(set, XDMF_SET_TYPE_NODE, &status);

  XDMFWRITER * writer = XdmfWriterNew("setfile.xmf");

  int testype = XdmfSetGetType(set);
  printf("%d ?= %d\n", testype, XDMF_SET_TYPE_NODE);
  assert(testype == XDMF_SET_TYPE_NODE);

  // Write to File

  XdmfSetAccept(set, (XDMFVISITOR *)writer, &status);
  XdmfSetFree(set);
  XdmfWriterFree(writer);

  // Read from File
  XDMFREADER * reader = XdmfReaderNew();
  XDMFITEM * readItem = XdmfReaderRead(reader, "setfile.xmf", &status);
  XdmfReaderFree(reader);
  XDMFSET * readSet = XdmfSetCast(readItem);
  XdmfItemFree(readItem);

  char * valueString = XdmfSetGetItemTag(readSet);
  printf("%s ?= %s\n", valueString, "Set");
  assert(strcmp(valueString, "Set") == 0);
  free(valueString);

  XdmfSetRead(readSet, &status);
  int settype = XdmfSetGetType(readSet);
  printf("Set type code = %d\n", settype);
  assert(settype == XDMF_SET_TYPE_NODE);

  valueString = XdmfSetGetValuesString(readSet);
  printf("array contains: %s\n", valueString);
  assert(strcmp("0 1 2 3 4 5 6 7 8 9", valueString) == 0);
  free(valueString);

  XdmfSetFree(readSet);

  return 0;
}
