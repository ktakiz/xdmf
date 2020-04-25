#include "XdmfDomain.hpp"
#include "XdmfReader.hpp"
#include "XdmfWriter.hpp"
#include <iostream>

#include "XdmfTestCompareFiles.hpp"
#include "XdmfTestDataGenerator.hpp"

int main(int, char **)
{
shared_ptr<XdmfDomain> domainR;
shared_ptr<XdmfDomain> domainM;
  shared_ptr<XdmfReader> reader = XdmfReader::New();
  {
	domainR = shared_dynamic_cast<XdmfDomain>(reader->read("tests/Cxx/without_ref.xmf"));

  }
  std::cout<<domainR<<std::endl;
  {
	domainM = shared_dynamic_cast<XdmfDomain>(reader->read("tests/Cxx/with_ref.xmf"));
  }
  std::cout<<domainM<<std::endl;

  return 0;
}
