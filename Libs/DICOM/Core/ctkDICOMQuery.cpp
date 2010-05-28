/*=========================================================================

  Library:   CTK
 
  Copyright (c) 2010  Kitware Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.commontk.org/LICENSE

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 
=========================================================================*/

// Qt includes
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QDate>
#include <QStringList>
#include <QSet>
#include <QFile>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

// ctkDICOM includes
#include "ctkDICOMQuery.h"
#include "ctkLogger.h"

// DCMTK includes
#ifndef WIN32
  #define HAVE_CONFIG_H 
#endif
#include "dcmtk/dcmnet/dimse.h"
#include "dcmtk/dcmnet/diutil.h"

#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcdatset.h>
#include <dcmtk/ofstd/ofcond.h>
#include <dcmtk/ofstd/ofstring.h>
#include <dcmtk/ofstd/ofstd.h>        /* for class OFStandard */
#include <dcmtk/dcmdata/dcddirif.h>   /* for class DicomDirInterface */

#include <dcmtk/dcmnet/scu.h>

static ctkLogger logger ( "org.commontk.core.Logger" );

//------------------------------------------------------------------------------
class ctkDICOMQueryPrivate: public ctkPrivate<ctkDICOMQuery>
{
public:
  ctkDICOMQueryPrivate();
  ~ctkDICOMQueryPrivate();
  QString CallingAETitle;
  QString CalledAETitle;
  QString Host;
  int Port;
  DcmSCU SCU;
  QSqlDatabase db;
  DcmDataset* query;

};

//------------------------------------------------------------------------------
// ctkDICOMQueryPrivate methods

//------------------------------------------------------------------------------
ctkDICOMQueryPrivate::ctkDICOMQueryPrivate()
{
  query = new DcmDataset();
}

//------------------------------------------------------------------------------
ctkDICOMQueryPrivate::~ctkDICOMQueryPrivate()
{
  delete query;
}


// Find callback
static void QueryCallback (void *callbackData, 
                           T_DIMSE_C_FindRQ* /*request*/, 
                           int /*responseCount*/, 
                           T_DIMSE_C_FindRSP* /*rsp*/, 
                           DcmDataset *responseIdentifiers) {
  ctkDICOMQueryPrivate* d = (ctkDICOMQueryPrivate*) callbackData;
  OFString StudyDescription;
  responseIdentifiers->findAndGetOFString ( DCM_StudyDescription, StudyDescription );
  logger.debug ( QString ( "Found study description: " ) + QString ( StudyDescription.c_str() ) );
}

//------------------------------------------------------------------------------
// ctkDICOMQuery methods

//------------------------------------------------------------------------------
ctkDICOMQuery::ctkDICOMQuery()
{
}

//------------------------------------------------------------------------------
ctkDICOMQuery::~ctkDICOMQuery()
{
}

/// Set methods for connectivity
void ctkDICOMQuery::setCallingAETitle ( QString callingAETitle )
{
  CTK_D(ctkDICOMQuery);
  d->CallingAETitle = callingAETitle;
}
const QString& ctkDICOMQuery::callingAETitle() 
{
  CTK_D(ctkDICOMQuery);
  return d->CallingAETitle;
}
void ctkDICOMQuery::setCalledAETitle ( QString calledAETitle )
{
  CTK_D(ctkDICOMQuery);
  d->CalledAETitle = calledAETitle;
}
const QString& ctkDICOMQuery::calledAETitle()
{
  CTK_D(ctkDICOMQuery);
  return d->CalledAETitle;
}
void ctkDICOMQuery::setHost ( QString host )
{
  CTK_D(ctkDICOMQuery);
  d->Host = host;
}
const QString& ctkDICOMQuery::host()
{
  CTK_D(ctkDICOMQuery);
  return d->Host;
}
void ctkDICOMQuery::setPort ( int port ) 
{
  CTK_D(ctkDICOMQuery);
  d->Port = port;
}
int ctkDICOMQuery::port()
{
  CTK_D(ctkDICOMQuery);
  return d->Port;
}



//------------------------------------------------------------------------------
void ctkDICOMQuery::query(QSqlDatabase database )
{
  CTK_D(ctkDICOMQuery);
  d->db = database;
  
  if ( logger.isDebugEnabled() )
    {
    std::cout << "Debugging ctkDICOMQuery" << std::endl;
    }
  d->SCU.setAETitle ( this->callingAETitle().toStdString() );
  d->SCU.setPeerAETitle ( this->calledAETitle().toStdString() );
  d->SCU.setPeerHostName ( this->host().toStdString() );
  d->SCU.setPeerPort ( this->port() );

  logger.error ( "Setting Transfer Syntaxes" );
  OFList<OFString> transferSyntaxes;
  transferSyntaxes.push_back ( UID_LittleEndianExplicitTransferSyntax );
  transferSyntaxes.push_back ( UID_BigEndianExplicitTransferSyntax );
  transferSyntaxes.push_back ( UID_LittleEndianImplicitTransferSyntax );

  // d->SCU.addPresentationContext ( UID_FINDStudyRootQueryRetrieveInformationModel, transferSyntaxes );
  d->SCU.addPresentationContext ( UID_VerificationSOPClass, transferSyntaxes );
  if ( !d->SCU.initNetwork().good() ) 
    {
    std::cerr << "Error initializing the network" << std::endl;
    return;
    }
  logger.debug ( "Negotiating Association" );
  d->SCU.negotiateAssociation();
  logger.debug ( "Sending Echo" );
  OFString abstractSyntax;
  OFString transferSyntax;
  if ( d->SCU.sendECHORequest ( 0 ).good() )
    {
    std::cout << "ECHO Sucessful" << std::endl;
    } 
  else
    {
    std::cerr << "ECHO Failed" << std::endl;
    }
  // Clear the query
  unsigned long elements = d->query->card();
  // Clean it out
  for ( unsigned long i = 0; i < elements; i++ ) 
    {
    d->query->remove ( (unsigned long) 0 );
    }
  d->query->insertEmptyElement ( DCM_QueryRetrieveLevel );
  d->query->insertEmptyElement ( DCM_PatientID );
  d->query->insertEmptyElement ( DCM_PatientsName );
  d->query->insertEmptyElement ( DCM_PatientsBirthDate );
  d->query->insertEmptyElement ( DCM_StudyID );
  d->query->insertEmptyElement ( DCM_StudyInstanceUID );
  d->query->insertEmptyElement ( DCM_StudyDescription );
  d->query->insertEmptyElement ( DCM_StudyDate );
  d->query->insertEmptyElement ( DCM_StudyID );
  d->query->insertEmptyElement ( DCM_PatientID );
  d->query->insertEmptyElement ( DCM_PatientsName );
  d->query->insertEmptyElement ( DCM_SeriesNumber );
  d->query->insertEmptyElement ( DCM_SeriesDescription );
  d->query->insertEmptyElement ( DCM_StudyInstanceUID );
  d->query->insertEmptyElement ( DCM_SeriesInstanceUID );
  d->query->insertEmptyElement ( DCM_StudyTime );
  d->query->insertEmptyElement ( DCM_SeriesDate );
  d->query->insertEmptyElement ( DCM_SeriesTime );
  d->query->insertEmptyElement ( DCM_Modality );
  d->query->insertEmptyElement ( DCM_ModalitiesInStudy );
  d->query->insertEmptyElement ( DCM_AccessionNumber );
  d->query->insertEmptyElement ( DCM_NumberOfSeriesRelatedInstances ); // Number of images in the series
  d->query->insertEmptyElement ( DCM_NumberOfStudyRelatedInstances ); // Number of images in the series
  d->query->insertEmptyElement ( DCM_NumberOfStudyRelatedSeries ); // Number of images in the series
  d->query->putAndInsertString ( DCM_QueryRetrieveLevel, "STUDY" );

  OFCondition status = d->SCU.sendFINDRequest ( 0, d->query, QueryCallback, (void*)d );
  if ( status.good() )
    {
    logger.debug ( "Find succeded" );
    }
  else
    {
    logger.error ( "Find failed" );
    }
  d->SCU.closeAssociation ( DUL_PEERREQUESTEDRELEASE );
}
