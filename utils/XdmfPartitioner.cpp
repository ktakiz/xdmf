/*******************************************************************/
/*                               XDMF                              */
/*                   eXtensible Data Model and Format              */
/*                                                                 */
/*  Id : Id  */
/*  Date : $Date$ */
/*  Version : $Revision$ */
/*                                                                 */
/*  Author:                                                        */
/*     Kenneth Leiter                                              */
/*     kenneth.leiter@arl.army.mil                                 */
/*     US Army Research Laboratory                                 */
/*     Aberdeen Proving Ground, MD                                 */
/*                                                                 */
/*     Copyright @ 2010 US Army Research Laboratory                */
/*     All Rights Reserved                                         */
/*     See Copyright.txt or http://www.arl.hpc.mil/ice for details */
/*                                                                 */
/*     This software is distributed WITHOUT ANY WARRANTY; without  */
/*     even the implied warranty of MERCHANTABILITY or FITNESS     */
/*     FOR A PARTICULAR PURPOSE.  See the above copyright notice   */
/*     for more information.                                       */
/*                                                                 */
/*******************************************************************/

#include "XdmfPartitioner.hpp"


#ifndef BUILD_EXE

extern "C"
{
	#include <metis.h>
}

#include <sstream>
#include "XdmfArray.hpp"
#include "XdmfAttribute.hpp"
#include "XdmfGeometry.hpp"
#include "XdmfGrid.hpp"
#include "XdmfGridCollection.hpp"
#include "XdmfHDF5Writer.hpp"
#include "XdmfTopology.hpp"

XdmfPartitioner::XdmfPartitioner()
{
}

XdmfPartitioner::~XdmfPartitioner()
{
}

boost::shared_ptr<XdmfGridCollection> XdmfPartitioner::partition(boost::shared_ptr<XdmfGrid> gridToPartition, const unsigned int numberOfPartitions,
		boost::shared_ptr<XdmfHDF5Writer> heavyDataWriter)
{
	int metisElementType;
	int numNodesPerElement;

	XdmfTopologyType topologyType = gridToPartition->getTopology()->getTopologyType();
	if(topologyType == XdmfTopologyType::Triangle() || topologyType == XdmfTopologyType::Triangle_6())
	{
		metisElementType = 1;
		numNodesPerElement = 3;
	}
	else if(topologyType == XdmfTopologyType::Quadrilateral() || topologyType == XdmfTopologyType::Quadrilateral_8())
	{
		metisElementType = 4;
		numNodesPerElement = 4;
	}
	else if(topologyType == XdmfTopologyType::Tetrahedron() || topologyType == XdmfTopologyType::Tetrahedron_10())
	{
		metisElementType = 2;
		numNodesPerElement = 4;
	}
	else if(topologyType == XdmfTopologyType::Hexahedron() || topologyType == XdmfTopologyType::Hexahedron_20() ||
			topologyType == XdmfTopologyType::Hexahedron_24() || topologyType == XdmfTopologyType::Hexahedron_27() ||
			topologyType == XdmfTopologyType::Hexahedron_64())
	{
		metisElementType = 3;
		numNodesPerElement = 8;
	}
	else
	{
		assert(false);
	}

	if(!gridToPartition->getTopology()->getArray()->isInitialized())
	{
		gridToPartition->getTopology()->getArray()->read();
	}

	int numElements = gridToPartition->getTopology()->getNumberElements();
	idxtype * metisConnectivity = new idxtype[numNodesPerElement * numElements];
	for(unsigned int i=0; i<numElements; ++i)
	{
		gridToPartition->getTopology()->getArray()->getValuesCopy(i*topologyType.getNodesPerElement(), &metisConnectivity[i*numNodesPerElement], numNodesPerElement);
	}

	int numNodes = gridToPartition->getGeometry()->getNumberPoints();

	// Need to remap connectivity for nonlinear elements so that metis handles it properly.
	std::map<idxtype, idxtype> xdmfIdToMetisId;
	if(numNodesPerElement != topologyType.getNodesPerElement())
	{
		int index = 0;
		for (unsigned int i=0; i<numElements * numNodesPerElement; ++i)
		{
			std::map<idxtype, idxtype>::const_iterator val = xdmfIdToMetisId.find(metisConnectivity[i]);
			if (val != xdmfIdToMetisId.end())
			{
				metisConnectivity[i] = val->second;
			}
			else
			{
				// Haven't seen this id before, map to index and set to new id
				xdmfIdToMetisId[metisConnectivity[i]] = index;
				metisConnectivity[i] = index;
				index++;
			}
		}
		numNodes = index;
	}

	int startIndex = 0;
	int numCutEdges = 0;

	idxtype * elementsPartition = new idxtype[numElements];
	idxtype * nodesPartition = new idxtype[numNodes];

	std::cout << "Entered METIS" << std::endl;
	METIS_PartMeshDual(&numElements, &numNodes, metisConnectivity, &metisElementType, &startIndex, (int*)&numberOfPartitions, &numCutEdges, elementsPartition, nodesPartition);
	//METIS_PartMeshNodal(&numElements, &numNodes, metisConnectivity, &metisElementType, &startIndex, &numPartitions, &numCutEdges, elementsPartition, nodesPartition);
	std::cout << "Exited METIS" << std::endl;

	delete [] metisConnectivity;
	delete [] nodesPartition;

	// For each partition, map global to local node id
	std::vector<std::map<unsigned int, unsigned int> > globalToLocalNodeIdMap;
	// For each partition, list global element id.
	std::vector<std::vector<unsigned int> > globalElementIds;
	for(unsigned int i=0; i<numberOfPartitions; ++i)
	{
		std::map<unsigned int, unsigned int> nodeMap;
		globalToLocalNodeIdMap.push_back(nodeMap);
		std::vector<unsigned int> elementIds;
		globalElementIds.push_back(elementIds);
	}

	// Fill in globalNodeId for each partition
	unsigned int totalIndex = 0;
	for (unsigned int i=0; i<numElements; ++i)
	{
		unsigned int partitionId = elementsPartition[i];
		for (unsigned int j=0; j<topologyType.getNodesPerElement(); ++j)
		{
			unsigned int globalNodeId = gridToPartition->getTopology()->getArray()->getValueCopy<unsigned int>(totalIndex);
			if (globalToLocalNodeIdMap[partitionId].count(globalNodeId) == 0)
			{
				// Have not seen this node, need to add to map
				int size = globalToLocalNodeIdMap[partitionId].size();
				globalToLocalNodeIdMap[partitionId][globalNodeId] = size;
			}
			totalIndex++;
		}
		globalElementIds[partitionId].push_back(i);
	}
	delete[] elementsPartition;

	boost::shared_ptr<XdmfAttribute> globalNodeIds = gridToPartition->getAttribute("GlobalNodeId");
	bool generateGlobalNodeIds = (globalNodeIds == NULL);

	boost::shared_ptr<XdmfGridCollection> partitionedGrids = XdmfGridCollection::New();
	partitionedGrids->setGridCollectionType(XdmfGridCollectionType::Spatial());

	if(!gridToPartition->getGeometry()->getArray()->isInitialized())
	{
		gridToPartition->getGeometry()->getArray()->read();
	}

	for(unsigned int i=0; i<numberOfPartitions; ++i)
	{
		std::map<unsigned int, unsigned int> & currNodeMap = globalToLocalNodeIdMap[i];
		std::vector<unsigned int> & currElemIds = globalElementIds[i];

		if(currElemIds.size() > 0)
		{
			std::stringstream name;
			name << gridToPartition->getName() << "_" << i;

			boost::shared_ptr<XdmfGrid> partitioned = XdmfGrid::New();
			partitioned->setName(name.str());
			partitionedGrids->insert(partitioned);

			// Fill in geometry for this partition
			partitioned->getGeometry()->setGeometryType(gridToPartition->getGeometry()->getGeometryType());
			boost::shared_ptr<XdmfArray> geometryVals = partitioned->getGeometry()->getArray();
			unsigned int numDimensions = partitioned->getGeometry()->getGeometryType().getDimensions();
			geometryVals->reserve(currNodeMap.size() * numDimensions);

			for(std::map<unsigned int, unsigned int>::const_iterator iter = currNodeMap.begin(); iter != currNodeMap.end(); ++iter)
			{
				geometryVals->copyValues(iter->second * numDimensions, gridToPartition->getGeometry()->getArray(), iter->first * numDimensions, numDimensions);
			}

			if(heavyDataWriter)
			{
				geometryVals->accept(heavyDataWriter);
				geometryVals->release();
			}

			// Fill in topology for this partition
			partitioned->getTopology()->setTopologyType(gridToPartition->getTopology()->getTopologyType());
			boost::shared_ptr<XdmfArray> topologyVals = partitioned->getTopology()->getArray();
			topologyVals->reserve(currElemIds.size() * topologyType.getNodesPerElement());

			unsigned int index = 0;
			for(std::vector<unsigned int>::const_iterator iter = currElemIds.begin(); iter != currElemIds.end(); ++iter)
			{
				// Translate these global node ids to local node ids
				for(unsigned int j=0; j<topologyType.getNodesPerElement(); ++j)
				{
					unsigned int globalNodeId = currNodeMap[gridToPartition->getTopology()->getArray()->getValueCopy<unsigned int>(*iter * topologyType.getNodesPerElement() + j)];
					topologyVals->copyValues(index, &globalNodeId, 1);
					index++;
				}
			}

			if(heavyDataWriter)
			{
				topologyVals->accept(heavyDataWriter);
				topologyVals->release();
			}

			if (generateGlobalNodeIds)
			{
				globalNodeIds = XdmfAttribute::New();
				globalNodeIds->setName("GlobalNodeId");
				globalNodeIds->setAttributeType(XdmfAttributeType::GlobalId());
				globalNodeIds->setAttributeCenter(XdmfAttributeCenter::Node());

				boost::shared_ptr<XdmfArray> globalNodeIdsVals = globalNodeIds->getArray();
				globalNodeIdsVals->reserve(currNodeMap.size());
				for (std::map<unsigned int, unsigned int>::const_iterator iter = currNodeMap.begin(); iter != currNodeMap.end(); ++iter)
				{
					globalNodeIdsVals->copyValues(iter->second, &iter->first, 1);
				}
				partitioned->insert(globalNodeIds);

				if(heavyDataWriter)
				{
					globalNodeIdsVals->accept(heavyDataWriter);
					globalNodeIdsVals->release();
				}
			}
		}
	}

	gridToPartition->getGeometry()->getArray()->release();
	gridToPartition->getTopology()->getArray()->release();

	for(unsigned int i=0; i<gridToPartition->getNumberOfAttributes(); ++i)
	{
		boost::shared_ptr<XdmfAttribute> currAttribute = gridToPartition->getAttribute(i);
		if(!currAttribute->getArray()->isInitialized())
		{
			currAttribute->getArray()->read();
		}
		unsigned int partitionId = 0;
		for(unsigned int j=0; j<numberOfPartitions; ++j)
		{
			std::map<unsigned int, unsigned int> & currNodeMap = globalToLocalNodeIdMap[j];
			std::vector<unsigned int> & currElemIds = globalElementIds[j];
			if(currElemIds.size() > 0)
			{
				boost::shared_ptr<XdmfGrid> partitioned = partitionedGrids->getGrid(partitionId);
				partitionId++;
				boost::shared_ptr<XdmfAttribute> createdAttribute;
				if(currAttribute->getAttributeCenter() == XdmfAttributeCenter::Grid())
				{
					if(partitionId == 0)
					{
						createdAttribute = currAttribute;
					}
				}
				else if(currAttribute->getAttributeCenter() == XdmfAttributeCenter::Cell() ||
						currAttribute->getAttributeCenter() == XdmfAttributeCenter::Face() ||
						currAttribute->getAttributeCenter() == XdmfAttributeCenter::Edge())
				{
					createdAttribute = XdmfAttribute::New();
					createdAttribute->setName(currAttribute->getName());
					createdAttribute->setAttributeCenter(currAttribute->getAttributeCenter());
					createdAttribute->setAttributeType(currAttribute->getAttributeType());
					unsigned int index = 0;
					unsigned int numValsPerComponent = currAttribute->getArray()->getSize() / gridToPartition->getTopology()->getNumberElements();
					createdAttribute->getArray()->reserve(currElemIds.size() * numValsPerComponent);
					for(std::vector<unsigned int>::const_iterator iter = currElemIds.begin(); iter != currElemIds.end(); ++iter)
					{
						createdAttribute->getArray()->copyValues(index, currAttribute->getArray(), *iter * numValsPerComponent, numValsPerComponent);
						index += numValsPerComponent;
					}
				}
				else if(currAttribute->getAttributeCenter() == XdmfAttributeCenter::Node())
				{
					createdAttribute = XdmfAttribute::New();
					createdAttribute->setName(currAttribute->getName());
					createdAttribute->setAttributeCenter(currAttribute->getAttributeCenter());
					createdAttribute->setAttributeType(currAttribute->getAttributeType());
					createdAttribute->getArray()->reserve(currNodeMap.size());
					for(std::map<unsigned int, unsigned int>::const_iterator iter = currNodeMap.begin(); iter != currNodeMap.end(); ++iter)
					{
						createdAttribute->getArray()->copyValues(iter->second, currAttribute->getArray(), iter->first, 1);
					}
				}
				if(createdAttribute != NULL)
				{
					partitioned->insert(createdAttribute);
					if(heavyDataWriter)
					{
						createdAttribute->getArray()->accept(heavyDataWriter);
						createdAttribute->getArray()->release();
					}
				}
			}
		}
		currAttribute->getArray()->release();
	}

	return partitionedGrids;
/*

  // Split sets and add to grid
  for(int j=0; j<grid->GetNumberOfSets(); ++j)
  {
    XdmfSet * currSet = grid->GetSets(j);
    // If data wasn't read in before, make sure it's released after processing
    bool releaseData = 0;
    if(currSet->GetIds()->GetNumberOfElements() == 0)
    {
      currSet->Update();
      releaseData = 1;
    }
    int partitionId = 0;
    for(int i=0; i<numPartitions; ++i)
    {
      std::map<XdmfInt32, XdmfInt32> currNodeMap = globalToLocalNodeIdMap[i];
      std::vector<XdmfInt32> currElemIds = globalElementIds[i];
      if(currElemIds.size() > 0)
      {
        XdmfGrid * partition = partitions[partitionId];
        partitionId++;
        switch(currSet->GetSetType())
        {
          case(XDMF_SET_TYPE_CELL):
          case(XDMF_SET_TYPE_FACE):
          case(XDMF_SET_TYPE_EDGE):
          {
            std::vector<XdmfInt32> myIds;
            for(int j=0; j<currSet->GetIds()->GetNumberOfElements(); ++j)
            {
              std::vector<XdmfInt32>::const_iterator val = std::find(currElemIds.begin(), currElemIds.end(), currSet->GetIds()->GetValueAsInt32(j));
              if(val != currElemIds.end())
              {
                myIds.push_back(val - currElemIds.begin());
              }
            }
            if(myIds.size() != 0)
            {
              XdmfSet * set = new XdmfSet();
              set->SetName(currSet->GetName());
              set->SetSetType(currSet->GetSetType());
              set->SetSize(myIds.size());
              set->SetDeleteOnGridDelete(true);
              XdmfArray * ids = set->GetIds();
              ids->SetNumberType(XDMF_INT32_TYPE);
              ids->SetNumberOfElements(myIds.size());
              ids->SetValues(0, &myIds[0], myIds.size());
              partition->Insert(set);
            }
            break;
          }
          case(XDMF_SET_TYPE_NODE):
          {
            std::vector<XdmfInt32> myIds;
            for(int j=0; j<currSet->GetIds()->GetNumberOfElements(); j++)
            {
              std::map<XdmfInt32, XdmfInt32>::const_iterator val = currNodeMap.find(currSet->GetIds()->GetValueAsInt32(j));
              if(val != currNodeMap.end())
              {
                myIds.push_back(val->second);
              }
            }
            if(myIds.size() != 0)
            {
              XdmfSet * set = new XdmfSet();
              set->SetName(currSet->GetName());
              set->SetSetType(currSet->GetSetType());
              set->SetSize(myIds.size());
              set->SetDeleteOnGridDelete(true);
              XdmfArray * ids = set->GetIds();
              ids->SetNumberType(XDMF_INT32_TYPE);
              ids->SetNumberOfElements(myIds.size());
              ids->SetValues(0, &myIds[0], myIds.size());
              partition->Insert(set);
            }
            break;
          }
          default:
          {
            std::cout << "Unknown set type encountered: " << currSet->GetSetTypeAsString() << std::endl;  
            break;
          }
        }
      }
    }
    if(releaseData)
    {
      currSet->Release();
    }
  }
  return collection;*/
}

#else

#include <sstream>
#include "XdmfDomain.hpp"
#include "XdmfGridCollection.hpp"
#include "XdmfHDF5Writer.hpp"
#include "XdmfReader.hpp"
#include "XdmfWriter.hpp"

/**
 * XdmfPartitioner is a command line utility for partitioning Xdmf grids.
 * The XdmfPartitioner uses the metis library to partition Triangular, Quadrilateral, Tetrahedral,
 * and Hexahedral XdmfGrids.
 *
 * Usage:
 *     XdmfPartitioner <path-of-file-to-partition> <num-partitions> (Optional: <path-to-output-file>)
 *
 */
int main(int argc, char* argv[])
{

	std::string usage = "Partitions an Xdmf grid using the metis library: \n \n Usage: \n \n   XdmfPartitioner <path-of-file-to-partition> <num-partitions> (Optional: <path-to-output-file>)";
	std::string meshName = "";

	if (argc < 3)
	{
		std::cout << usage << std::endl;
		return 1;
	}

	FILE * refFile = fopen(argv[1], "r");
	if (refFile)
	{
		// Success
		meshName = argv[1];
		fclose(refFile);
	}
	else
	{
		std::cout << "Cannot open file: " << argv[1] << std::endl;
		return 1;
	}

	unsigned int numPartitions = atoi(argv[2]);

	if (argc >= 4)
	{
		meshName = argv[3];
	}

	if(meshName.find_last_of("/\\") != std::string::npos)
	{
		meshName = meshName.substr(meshName.find_last_of("/\\")+1, meshName.length());
	}

	if (meshName.rfind(".") != std::string::npos)
	{
		meshName = meshName.substr(0, meshName.rfind("."));
	}

	if(argc < 4)
	{
		meshName = meshName + "-partitioned";
	}

	boost::shared_ptr<XdmfReader> reader = XdmfReader::New();
	boost::shared_ptr<XdmfDomain> domain = boost::shared_dynamic_cast<XdmfDomain>(reader->read(argv[1]));

	if(domain->getNumberOfGrids() <= 0)
	{
		std::cout << "No grids to partition" << std::endl;
		return 1;
	}

	std::stringstream heavyFileName;
	heavyFileName << meshName << ".h5";
	boost::shared_ptr<XdmfHDF5Writer> heavyDataWriter = XdmfHDF5Writer::New(heavyFileName.str());

	boost::shared_ptr<XdmfPartitioner> partitioner = XdmfPartitioner::New();
	boost::shared_ptr<XdmfGridCollection> toWrite = partitioner->partition(domain->getGrid(0), numPartitions, heavyDataWriter);

	boost::shared_ptr<XdmfDomain> newDomain = XdmfDomain::New();
	newDomain->insert(toWrite);

	std::stringstream xmlFileName;
	xmlFileName << meshName << ".xmf";
	boost::shared_ptr<XdmfWriter> writer = XdmfWriter::New(xmlFileName.str(), heavyDataWriter);
	newDomain->accept(writer);


	/*XdmfDOM dom;
	XdmfInt32 error = dom.Parse(argv[1]);

	std::string fileName = argv[1];
	size_t fileNameFound = fileName.find_last_of("/\\");
	if (fileNameFound != std::string::npos)
	{
		dom.SetWorkingDirectory(fileName.substr(0, fileNameFound).substr().c_str());
	}

	if(error == XDMF_FAIL)
	{
		std::cout << "File does not appear to be a valid Xdmf file" << std::endl;
		return 1;
	}
	XdmfXmlNode gridElement = dom.FindElementByPath("/Xdmf/Domain/Grid");
	if(gridElement == NULL)
	{
		std::cout << "Cannot parse Xdmf file!" << std::endl;
		return 1;
	}

	XdmfGrid * grid = new XdmfGrid();
	grid->SetDOM(&dom);
	grid->SetElement(gridElement);
	grid->Update();

	XdmfDOM newDOM;
	XdmfRoot newRoot;
	XdmfDomain newDomain;

	newRoot.SetDOM(&newDOM);
	newRoot.Build();
	newRoot.Insert(&newDomain);

	XdmfPartitioner partitioner;
	XdmfGrid * partitionedGrid = partitioner.Partition(grid, numPartitions, &newDomain);
	delete grid;

	for(int i=0; i<partitionedGrid->GetNumberOfChildren(); ++i)
	{
		XdmfGrid * child = partitionedGrid->GetChild(i);

		// Set heavy data set names for geometry and topology
		std::stringstream heavyPointName;
		heavyPointName << meshName << ".h5:/" << child->GetName() << "/XYZ";
		child->GetGeometry()->GetPoints()->SetHeavyDataSetName(heavyPointName.str().c_str());

		std::stringstream heavyConnName;
		heavyConnName << meshName << ".h5:/" << child->GetName() << "/Connections";
		child->GetTopology()->GetConnectivity()->SetHeavyDataSetName(heavyConnName.str().c_str());

		// Set heavy data set names for mesh attributes and sets
		for(int i=0; i<child->GetNumberOfAttributes(); i++)
		{
			std::stringstream heavyAttrName;
			heavyAttrName << meshName << ".h5:/" << child->GetName() << "/Attribute/" << child->GetAttribute(i)->GetAttributeCenterAsString() << "/" << child->GetAttribute(i)->GetName();
			child->GetAttribute(i)->GetValues()->SetHeavyDataSetName(heavyAttrName.str().c_str());
		}

		for(int i=0; i<child->GetNumberOfSets(); i++)
		{
			std::stringstream heavySetName;
			heavySetName << meshName << ".h5:/" << child->GetName() << "/Set/" << child->GetSets(i)->GetSetTypeAsString() << "/" << child->GetSets(i)->GetName();
			child->GetSets(i)->GetIds()->SetHeavyDataSetName(heavySetName.str().c_str());
		}
	}

	partitionedGrid->Build();

	std::stringstream outputFileName;
	outputFileName << meshName << ".xmf";

	newDOM.Write(outputFileName.str().c_str());

	delete partitionedGrid;
	std::cout << "Wrote: " << outputFileName.str().c_str() << std::endl;*/
}

#endif //BUILD_EXE