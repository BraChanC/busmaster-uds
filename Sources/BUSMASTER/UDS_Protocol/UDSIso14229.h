/**
 * ISO 14229-1 UDS request services and sub-functions.
 * Covers ISO 14229-1:2013 and 2020 (includes 0x29 Authentication, 0x38 RequestFileTransfer).
 */
#pragma once

struct UdsSubFn
{
    BYTE m_bySf;
    const char* m_pchName;
    const char* m_pchExtra;   /* optional extra payload bytes after SID+SF, hex without spaces */
    const char* m_pchAttr;    /* attribute text shown in the list */
    const char* m_pchGroup;   /* optional parent folder name in the service tree */
};

struct UdsServiceDef
{
    BYTE m_bySid;
    const char* m_pchName;
    const UdsSubFn* m_psSub;
    int m_nSub;
    bool m_bCyclicHint;
    bool m_bEmitSf;
};

static const UdsSubFn kSub10[] =
{
    {0x01, "defaultSession", "", "subFunction=01h defaultSession"},
    {0x02, "programmingSession", "", "subFunction=02h programmingSession"},
    {0x03, "extendedDiagnosticSession", "", "subFunction=03h extendedDiagnosticSession"},
    {0x04, "safetySystemDiagnosticSession", "", "subFunction=04h safetySystemDiagnosticSession"},
};
static const UdsSubFn kSub11[] =
{
    {0x01, "hardReset", "", "subFunction=01h hardReset"},
    {0x02, "keyOffOnReset", "", "subFunction=02h keyOffOnReset"},
    {0x03, "softReset", "", "subFunction=03h softReset"},
    {0x04, "enableRapidPowerShutDown", "", "subFunction=04h enableRapidPowerShutDown"},
    {0x05, "disableRapidPowerShutDown", "", "subFunction=05h disableRapidPowerShutDown"},
};
static const UdsSubFn kSub14[] =
{
    {0x00, "Clear all DTC", "FFFFFF", "groupOfDTC=FFFFFFh (all groups)", "ClearDiagnosticInformation"},
};
static const UdsSubFn kSub19[] =
{
    {0x01, "reportNumberOfDTCByStatusMask", "FF", "DTCStatusMask=FFh"},
    {0x02, "reportDTCByStatusMask", "FF", "DTCStatusMask=FFh"},
    {0x03, "reportDTCSnapshotIdentification", "", "no extra bytes"},
    {0x04, "reportDTCSnapshotRecordByDTCNumber", "000000FF", "DTC+recordNumber"},
    {0x05, "reportDTCStoredDataByRecordNumber", "01", "DTCStoredDataRecordNumber"},
    {0x06, "reportDTCExtDataRecordByDTCNumber", "000000FF", "DTC+ExtDataRecordNumber"},
    {0x07, "reportNumberOfDTCBySeverityMaskRecord", "FFFF", "DTCSeverityMask+DTCStatusMask"},
    {0x08, "reportDTCBySeverityMaskRecord", "FFFF", "DTCSeverityMask+DTCStatusMask"},
    {0x09, "reportSeverityInformationOfDTC", "000000", "DTCMaskRecord"},
    {0x0A, "reportSupportedDTC", "", "no extra bytes"},
    {0x0B, "reportFirstTestFailedDTC", "", "no extra bytes"},
    {0x0C, "reportFirstConfirmedDTC", "", "no extra bytes"},
    {0x0D, "reportMostRecentTestFailedDTC", "", "no extra bytes"},
    {0x0E, "reportMostRecentConfirmedDTC", "", "no extra bytes"},
    {0x0F, "reportMirrorMemoryDTCByStatusMask", "FF", "DTCStatusMask"},
    {0x10, "reportMirrorMemoryDTCExtDataRecordByDTCNumber", "000000FF", "DTC+ExtDataRecordNumber"},
    {0x11, "reportNumberOfMirrorMemoryDTCByStatusMask", "FF", "DTCStatusMask"},
    {0x12, "reportNumberOfEmissionsRelatedOBDDTCByStatusMask", "FF", "DTCStatusMask"},
    {0x13, "reportEmissionsRelatedOBDDTCByStatusMask", "FF", "DTCStatusMask"},
    {0x14, "reportDTCFaultDetectionCounter", "", "no extra bytes"},
    {0x15, "reportDTCWithPermanentStatus", "", "no extra bytes"},
    {0x16, "reportDTCExtDataRecordByRecordNumber", "FF", "DTCExtDataRecordNumber"},
    {0x17, "reportUserDefMemoryDTCByStatusMask", "FF00", "DTCStatusMask+MemorySelection"},
    {0x18, "reportUserDefMemoryDTCSnapshotRecordByDTCNumber", "000000FF00", "DTC+record+MemorySelection"},
    {0x19, "reportUserDefMemoryDTCExtDataRecordByDTCNumber", "000000FF00", "DTC+ExtData+MemorySelection"},
    {0x1A, "reportSupportedDTCExtDataRecord", "", "no extra bytes"},
    {0x42, "reportWWHOBDDTCByMaskRecord", "FFFF", "FunctionalGroup+StatusMask"},
    {0x55, "reportWWHOBDDTCWithPermanentStatus", "FF", "FunctionalGroupIdentifier"},
    {0x56, "reportDTCInformationByDTCReadinessGroupIdentifier", "FFFF", "FunctionalGroup+ReadinessGroup"},
};
static const UdsSubFn kSub22[] =
{
    {0x00, "bootSoftwareIdentificationDataIdentifier (22 F1 80)", "F180", "DID=F180h BootSoftwareIdentification"},
    {0x00, "applicationSoftwareIdentificationDataIdentifier (22 F1 81)", "F181", "DID=F181h applicationSoftwareIdentification"},
    {0x00, "applicationDataIdentificationDataIdentifier (22 F1 82)", "F182", "DID=F182h applicationDataIdentification"},
    {0x00, "bootSoftwareFingerprintDataIdentifier (22 F1 83)", "F183", "DID=F183h bootSoftwareFingerprint"},
    {0x00, "applicationSoftwareFingerprintDataIdentifier (22 F1 84)", "F184", "DID=F184h applicationSoftwareFingerprint"},
    {0x00, "applicationDataFingerprintDataIdentifier (22 F1 85)", "F185", "DID=F185h applicationDataFingerprint"},
    {0x00, "activeDiagnosticSessionDataIdentifier (22 F1 86)", "F186", "DID=F186h ActiveDiagnosticSession"},
    {0x00, "vehicleManufacturerSparePartNumberDataIdentifier (22 F1 87)", "F187", "DID=F187h vehicleManufacturerSparePartNumber"},
    {0x00, "vehicleManufacturerECUSoftwareNumberDataIdentifier (22 F1 88)", "F188", "DID=F188h vehicleManufacturerECUSoftwareNumber"},
    {0x00, "vehicleManufacturerECUSoftwareVersionNumberDataIdentifier (22 F1 89)", "F189", "DID=F189h vehicleManufacturerECUSoftwareVersionNumber"},
    {0x00, "systemSupplierIdentifierDataIdentifier (22 F1 8A)", "F18A", "DID=F18Ah systemSupplierIdentifier"},
    {0x00, "ECUManufacturingDateDataIdentifier (22 F1 8B)", "F18B", "DID=F18Bh ECUManufacturingDate"},
    {0x00, "ECUSerialNumberDataIdentifier (22 F1 8C)", "F18C", "DID=F18Ch ECUSerialNumber"},
    {0x00, "supportedFunctionalUnitsDataIdentifier (22 F1 8D)", "F18D", "DID=F18Dh supportedFunctionalUnits"},
    {0x00, "vehicleManufacturerKitAssemblyPartNumberDataIdentifier (22 F1 8E)", "F18E", "DID=F18Eh vehicleManufacturerKitAssemblyPartNumber"},
    {0x00, "regulationXSoftwareIdentificationNumbers (22 F1 8F)", "F18F", "DID=F18Fh RegulationXSoftwareIdentificationNumbers"},
    {0x00, "VINDataIdentifier (22 F1 90)", "F190", "DID=F190h VIN"},
    {0x00, "vehicleManufacturerECUHardwareNumberDataIdentifier (22 F1 91)", "F191", "DID=F191h vehicleManufacturerECUHardwareNumber"},
    {0x00, "systemSupplierECUHardwareNumberDataIdentifier (22 F1 92)", "F192", "DID=F192h systemSupplierECUHardwareNumber"},
    {0x00, "systemSupplierECUHardwareVersionNumberDataIdentifier (22 F1 93)", "F193", "DID=F193h systemSupplierECUHardwareVersionNumber"},
    {0x00, "systemSupplierECUSoftwareNumberDataIdentifier (22 F1 94)", "F194", "DID=F194h systemSupplierECUSoftwareNumber"},
    {0x00, "systemSupplierECUSoftwareVersionNumberDataIdentifier (22 F1 95)", "F195", "DID=F195h systemSupplierECUSoftwareVersionNumber"},
    {0x00, "exhaustRegulationOrTypeApprovalNumberDataIdentifier (22 F1 96)", "F196", "DID=F196h exhaustRegulationOrTypeApprovalNumber"},
    {0x00, "systemNameOrEngineTypeDataIdentifier (22 F1 97)", "F197", "DID=F197h systemNameOrEngineType"},
    {0x00, "repairShopCodeOrTesterSerialNumberDataIdentifier (22 F1 98)", "F198", "DID=F198h repairShopCodeOrTesterSerialNumber"},
    {0x00, "programmingDateDataIdentifier (22 F1 99)", "F199", "DID=F199h programmingDate"},
    {0x00, "calibrationRepairShopCodeOrCalibrationEquipmentSerialNumber (22 F1 9A)", "F19A", "DID=F19Ah calibrationRepairShopCodeOrCalibrationEquipmentSerialNumber"},
    {0x00, "calibrationDateDataIdentifier (22 F1 9B)", "F19B", "DID=F19Bh calibrationDate"},
    {0x00, "calibrationEquipmentSoftwareNumberDataIdentifier (22 F1 9C)", "F19C", "DID=F19Ch calibrationEquipmentSoftwareNumber"},
    {0x00, "ECUInstallationDateDataIdentifier (22 F1 9D)", "F19D", "DID=F19Dh ECUInstallationDate"},
    {0x00, "ODXFileDataIdentifier (22 F1 9E)", "F19E", "DID=F19Eh ODXFile"},
    {0x00, "entityDataIdentifier (22 F1 9F)", "F19F", "DID=F19Fh Entity"},
    {0x00, "numberOfEDRDevices (22 F1 A0)", "F1A0", "DID=F1A0h NumberOfEDRDevices"},
    {0x00, "EDRIdentification (22 F1 A1)", "F1A1", "DID=F1A1h EDRIdentification"},
    {0x00, "EDRDeviceAddressInformation (22 F1 A2)", "F1A2", "DID=F1A2h EDRDeviceAddressInformation"},
    {0x00, "EDRStatus (22 F1 A5)", "F1A5", "DID=F1A5h EDRStatus"},
    {0x00, "UDSVersionDataIdentifier (22 F1 A8)", "F1A8", "DID=F1A8h UDSVersion"},
    {0x00, "ECUOperationalState (22 F1 A9)", "F1A9", "DID=F1A9h ECUOperationalState"},
    {0xFF, "Custom ReadDataByIdentifier", "", "user defined dataIdentifier (edit Data Bytes: 22 + DID)"},
};
static const UdsSubFn kSub23[] =
{
    {0x00, "Read 4 bytes from 00000000 (demo)", "440000000000000004", "ALFID=44h address=00000000h size=00000004h", "ReadMemoryByAddress (LL=0x44)"},
    {0x00, "Read 2 bytes from 0000 (demo)", "2200000002", "ALFID=22h address=0000h size=0002h", "ReadMemoryByAddress (LL=0x22)"},
};
static const UdsSubFn kSub24[] =
{
    {0x00, "DID F190", "F190", "dataIdentifier=F190h"},
};
static const UdsSubFn kSub27[] =
{
    {0x01, "requestSeed level 01", "", "subFunction=01h requestSeed"},
    {0x02, "sendKey level 01", "00000000", "subFunction=02h sendKey + securityKey"},
    {0x03, "requestSeed level 02", "", "subFunction=03h requestSeed"},
    {0x04, "sendKey level 02", "00000000", "subFunction=04h sendKey + securityKey"},
};
static const UdsSubFn kSub28[] =
{
    {0x00, "ALL messages (28 00 03)", "03", "communicationType=03h ALL messages", "EnableRxAndTx"},
    {0x00, "normalCommunicationMessages (28 00 01)", "01", "communicationType=01h normalCommunicationMessages", "EnableRxAndTx"},
    {0x00, "networkManagementCommunicationMessages (28 00 02)", "02", "communicationType=02h networkManagementCommunicationMessages", "EnableRxAndTx"},
    {0x01, "ALL messages (28 01 03)", "03", "communicationType=03h ALL messages", "EnableRxAndDisableTx"},
    {0x01, "normalCommunicationMessages (28 01 01)", "01", "communicationType=01h normalCommunicationMessages", "EnableRxAndDisableTx"},
    {0x01, "networkManagementCommunicationMessages (28 01 02)", "02", "communicationType=02h networkManagementCommunicationMessages", "EnableRxAndDisableTx"},
    {0x02, "ALL messages (28 02 03)", "03", "communicationType=03h ALL messages", "DisableRxAndEnableTx"},
    {0x02, "normalCommunicationMessages (28 02 01)", "01", "communicationType=01h normalCommunicationMessages", "DisableRxAndEnableTx"},
    {0x02, "networkManagementCommunicationMessages (28 02 02)", "02", "communicationType=02h networkManagementCommunicationMessages", "DisableRxAndEnableTx"},
    {0x03, "ALL messages (28 03 03)", "03", "communicationType=03h ALL messages", "DisableRxAndTx"},
    {0x03, "normalCommunicationMessages (28 03 01)", "01", "communicationType=01h normalCommunicationMessages", "DisableRxAndTx"},
    {0x03, "networkManagementCommunicationMessages (28 03 02)", "02", "communicationType=02h networkManagementCommunicationMessages", "DisableRxAndTx"},
    {0xFF, "Custom CommunicationControl", "", "user defined controlType + communicationType", nullptr},
};
static const UdsSubFn kSub29[] =
{
    {0x00, "deAuthenticate", "", "subFunction=00h deAuthenticate"},
    {0x01, "verifyCertificateUnidirectional", "", "subFunction=01h verifyCertificateUnidirectional"},
    {0x02, "verifyCertificateBidirectional", "", "subFunction=02h verifyCertificateBidirectional"},
    {0x03, "proofOfOwnership", "", "subFunction=03h proofOfOwnership"},
    {0x04, "transmitCertificate", "", "subFunction=04h transmitCertificate"},
    {0x05, "requestChallengeForAuthentication", "", "subFunction=05h requestChallengeForAuthentication"},
    {0x06, "verifyProofOfOwnershipUnidirectional", "", "subFunction=06h verifyProofOfOwnershipUnidirectional"},
    {0x07, "verifyProofOfOwnershipBidirectional", "", "subFunction=07h verifyProofOfOwnershipBidirectional"},
    {0x08, "authenticationConfiguration", "", "subFunction=08h authenticationConfiguration"},
};
static const UdsSubFn kSub2A[] =
{
    {0x01, "sendAtSlowRate", "F2", "transmissionMode=01h periodicDataIdentifier"},
    {0x02, "sendAtMediumRate", "F2", "transmissionMode=02h periodicDataIdentifier"},
    {0x03, "sendAtFastRate", "F2", "transmissionMode=03h periodicDataIdentifier"},
    {0x04, "stopSending", "F2", "transmissionMode=04h stopSending"},
};
static const UdsSubFn kSub2C[] =
{
    {0x01, "defineByIdentifier", "F300F190", "DDDID + source DID"},
    {0x02, "defineByMemoryAddress", "F3001400000001", "DDDID + memoryAddress"},
    {0x03, "clearDynamicallyDefinedDataIdentifier", "F300", "clear DDDID F300h (empty=clear all)"},
};
static const UdsSubFn kSub2E[] =
{
    {0x00, "bootSoftwareIdentificationDataIdentifier (2E F1 80)", "F18000", "DID=F180h + dataRecord"},
    {0x00, "applicationSoftwareIdentificationDataIdentifier (2E F1 81)", "F18100", "DID=F181h + dataRecord"},
    {0x00, "applicationDataIdentificationDataIdentifier (2E F1 82)", "F18200", "DID=F182h + dataRecord"},
    {0x00, "bootSoftwareFingerprintDataIdentifier (2E F1 83)", "F18300", "DID=F183h + dataRecord"},
    {0x00, "applicationSoftwareFingerprintDataIdentifier (2E F1 84)", "F18400", "DID=F184h + dataRecord"},
    {0x00, "applicationDataFingerprintDataIdentifier (2E F1 85)", "F18500", "DID=F185h + dataRecord"},
    {0x00, "activeDiagnosticSessionDataIdentifier (2E F1 86)", "F18600", "DID=F186h + dataRecord"},
    {0x00, "vehicleManufacturerSparePartNumberDataIdentifier (2E F1 87)", "F18700", "DID=F187h + dataRecord"},
    {0x00, "vehicleManufacturerECUSoftwareNumberDataIdentifier (2E F1 88)", "F18800", "DID=F188h + dataRecord"},
    {0x00, "vehicleManufacturerECUSoftwareVersionNumberDataIdentifier (2E F1 89)", "F18900", "DID=F189h + dataRecord"},
    {0x00, "systemSupplierIdentifierDataIdentifier (2E F1 8A)", "F18A00", "DID=F18Ah + dataRecord"},
    {0x00, "ECUManufacturingDateDataIdentifier (2E F1 8B)", "F18B00", "DID=F18Bh + dataRecord (YYMMDD)"},
    {0x00, "ECUSerialNumberDataIdentifier (2E F1 8C)", "F18C00", "DID=F18Ch + dataRecord"},
    {0x00, "supportedFunctionalUnitsDataIdentifier (2E F1 8D)", "F18D00", "DID=F18Dh + dataRecord"},
    {0x00, "vehicleManufacturerKitAssemblyPartNumberDataIdentifier (2E F1 8E)", "F18E00", "DID=F18Eh + dataRecord"},
    {0x00, "regulationXSoftwareIdentificationNumbers (2E F1 8F)", "F18F00", "DID=F18Fh + dataRecord"},
    {0x00, "VINDataIdentifier (2E F1 90)", "F19000", "DID=F190h VIN + dataRecord"},
    {0x00, "vehicleManufacturerECUHardwareNumberDataIdentifier (2E F1 91)", "F19100", "DID=F191h + dataRecord"},
    {0x00, "systemSupplierECUHardwareNumberDataIdentifier (2E F1 92)", "F19200", "DID=F192h + dataRecord"},
    {0x00, "systemSupplierECUHardwareVersionNumberDataIdentifier (2E F1 93)", "F19300", "DID=F193h + dataRecord"},
    {0x00, "systemSupplierECUSoftwareNumberDataIdentifier (2E F1 94)", "F19400", "DID=F194h + dataRecord"},
    {0x00, "systemSupplierECUSoftwareVersionNumberDataIdentifier (2E F1 95)", "F19500", "DID=F195h + dataRecord"},
    {0x00, "exhaustRegulationOrTypeApprovalNumberDataIdentifier (2E F1 96)", "F19600", "DID=F196h + dataRecord"},
    {0x00, "systemNameOrEngineTypeDataIdentifier (2E F1 97)", "F19700", "DID=F197h + dataRecord"},
    {0x00, "repairShopCodeOrTesterSerialNumberDataIdentifier (2E F1 98)", "F19800", "DID=F198h + dataRecord"},
    {0x00, "programmingDateDataIdentifier (2E F1 99)", "F19900", "DID=F199h + dataRecord (YYMMDD)"},
    {0x00, "calibrationRepairShopCodeOrCalibrationEquipmentSerialNumber (2E F1 9A)", "F19A00", "DID=F19Ah + dataRecord"},
    {0x00, "calibrationDateDataIdentifier (2E F1 9B)", "F19B00", "DID=F19Bh + dataRecord (YYMMDD)"},
    {0x00, "calibrationEquipmentSoftwareNumberDataIdentifier (2E F1 9C)", "F19C00", "DID=F19Ch + dataRecord"},
    {0x00, "ECUInstallationDateDataIdentifier (2E F1 9D)", "F19D00", "DID=F19Dh + dataRecord (YYMMDD)"},
    {0x00, "ODXFileDataIdentifier (2E F1 9E)", "F19E00", "DID=F19Eh + dataRecord"},
    {0x00, "entityDataIdentifier (2E F1 9F)", "F19F00", "DID=F19Fh + dataRecord"},
    {0x00, "numberOfEDRDevices (2E F1 A0)", "F1A000", "DID=F1A0h + dataRecord"},
    {0x00, "EDRIdentification (2E F1 A1)", "F1A100", "DID=F1A1h + dataRecord"},
    {0x00, "EDRDeviceAddressInformation (2E F1 A2)", "F1A200", "DID=F1A2h + dataRecord"},
    {0x00, "EDRStatus (2E F1 A5)", "F1A500", "DID=F1A5h + dataRecord"},
    {0x00, "UDSVersionDataIdentifier (2E F1 A8)", "F1A800", "DID=F1A8h + dataRecord"},
    {0x00, "ECUOperationalState (2E F1 A9)", "F1A900", "DID=F1A9h + dataRecord"},
    {0xFF, "Custom WriteDataByIdentifier", "", "user defined dataIdentifier + dataRecord (edit Data Bytes: 2E + DID + data)"},
};
static const UdsSubFn kSub2F[] =
{
    {0x00, "returnControlToECU", "F10000", "DID + IOCP=00h returnControlToECU"},
    {0x00, "resetToDefault", "F10001", "DID + IOCP=01h resetToDefault"},
    {0x00, "freezeCurrentState", "F10002", "DID + IOCP=02h freezeCurrentState"},
    {0x00, "shortTermAdjustment", "F10003", "DID + IOCP=03h shortTermAdjustment + controlState"},
};
#define UDS_RID(sf, group, name, rid, desc) {sf, name, rid, desc, group}
#define UDS_RID_LIST(sf, group) \
    UDS_RID(sf, group, "TachographTestIds (01 00)", "0100", "0100-01FFh TachographTestIds"), \
    UDS_RID(sf, group, "vehicleManufacturerSpecific (02 00)", "0200", "0200-DFFFh vehicleManufacturerSpecific"), \
    UDS_RID(sf, group, "0201 (02 01)", "0201", "0200-DFFFh vehicleManufacturerSpecific"), \
    UDS_RID(sf, group, "DF01 (DF 01)", "DF01", "0200-DFFFh vehicleManufacturerSpecific"), \
    UDS_RID(sf, group, "OBDTestIds (E0 00)", "E000", "E000-E1FFh OBD/EOBD TestIds"), \
    UDS_RID(sf, group, "DeployLoopRoutineID (E2 00)", "E200", "E200h DeployLoopRoutineID"), \
    UDS_RID(sf, group, "SafetySystemRoutineIDs (E2 01)", "E201", "E201-E2FFh SafetySystemRoutineIDs"), \
    UDS_RID(sf, group, "systemSupplierSpecific (F0 00)", "F000", "F000-FEFFh systemSupplierSpecific"), \
    UDS_RID(sf, group, "eraseMemory (FF 00)", "FF00", "FF00h eraseMemory"), \
    UDS_RID(sf, group, "checkProgrammingDependencies (FF 01)", "FF01", "FF01h checkProgrammingDependencies"), \
    UDS_RID(sf, group, "eraseMirrorMemoryDTCs (FF 02)", "FF02", "FF02h eraseMirrorMemoryDTCs"), \
    UDS_RID(sf, group, "Custom Routine ID", "", "edit Data Bytes: add routineIdentifier (and optional routineControlOptionRecord)")
static const UdsSubFn kSub31[] =
{
    UDS_RID_LIST(0x01, "startRoutine"),
    UDS_RID_LIST(0x02, "stopRoutine"),
    UDS_RID_LIST(0x03, "requestRoutineResults"),
};
#undef UDS_RID_LIST
#undef UDS_RID
static const UdsSubFn kSub34[] =
{
    {0x00, "4 bytes at 00000000 (demo)", "00440000000000000004", "DFI=00h ALFID=44h address=00000000h size=00000004h", "RequestDownload(LL=0x44)"},
    {0x00, "2 bytes at 0000 (demo)", "002200000002", "DFI=00h ALFID=22h address=0000h size=0002h", "RequestDownload(LL=0x22)"},
};
static const UdsSubFn kSub35[] =
{
    {0x00, "4 bytes at 00000000 (demo)", "00440000000000000004", "DFI=00h ALFID=44h address=00000000h size=00000004h", "RequestUpload(LL=0x44)"},
    {0x00, "2 bytes at 0000 (demo)", "002200000002", "DFI=00h ALFID=22h address=0000h size=0002h", "RequestUpload(LL=0x22)"},
};
static const UdsSubFn kSub36[] =
{
    {0x00, "blockSequenceCounter + data", "01", "blockSequenceCounter=01h + transferRequestParameterRecord"},
};
static const UdsSubFn kSub37[] =
{
    {0x00, "no parameters", "", "transferRequestParameterRecord optional"},
};
static const UdsSubFn kSub38[] =
{
    {0x01, "AddFile", "0100042E62696E", "modeOfOperation=01h AddFile + filePathAndName"},
    {0x02, "DeleteFile", "0200042E62696E", "modeOfOperation=02h DeleteFile"},
    {0x03, "ReplaceFile", "0300042E62696E", "modeOfOperation=03h ReplaceFile"},
    {0x04, "ReadFile", "0400042E62696E", "modeOfOperation=04h ReadFile"},
    {0x05, "ReadDir", "0500012E", "modeOfOperation=05h ReadDir"},
    {0x06, "ResumeFile", "0600042E62696E", "modeOfOperation=06h ResumeFile"},
};
static const UdsSubFn kSub3D[] =
{
    {0x00, "Write 4 bytes to 00000000 (demo)", "44000000000000000400000000", "ALFID=44h address=00000000h size=00000004h + dataRecord", "WriteMemoryByAddress (LL=0x44)"},
    {0x00, "Write 2 bytes to 0000 (demo)", "22000000020000", "ALFID=22h address=0000h size=0002h + dataRecord", "WriteMemoryByAddress (LL=0x22)"},
};
static const UdsSubFn kSub3E[] =
{
    {0x00, "zeroSubFunction", "", "subFunction=00h (positive response required)"},
    {0x80, "suppressPosRspMsgIndicationBit", "", "subFunction=80h (no positive response)"},
};
static const UdsSubFn kSub83[] =
{
    {0x01, "readExtendedTimingParameterSet (83 01)", "", "subFunction=01h readExtendedTimingParameterSet", "readExtendedTimingParameterSet"},
    {0x02, "setTimingParametersToDefaultValues (83 02)", "", "subFunction=02h setTimingParametersToDefaultValues", "setTimingParametersToDefaultValues"},
    {0x03, "readCurrentlyActiveTimingParameters (83 03)", "", "subFunction=03h readCurrentlyActiveTimingParameters", "readCurrentlyActiveTimingParameters"},
    {0x04, "setTimingParametersToGivenValues (83 04)", "00", "subFunction=04h + TimingParameterRequestRecord", "setTimingParametersToGivenValues"},
};
static const UdsSubFn kSub84[] =
{
    {0x00, "administrativeParameter + signature", "0000", "administrativeParameter + Signature/Filler/Data"},
};
static const UdsSubFn kSub85[] =
{
    {0x01, "on", "", "DTCSettingType=01h on"},
    {0x02, "off", "", "DTCSettingType=02h off"},
};
static const UdsSubFn kSub86[] =
{
    {0x00, "stopResponseOnEvent", "", "eventType=00h stopResponseOnEvent"},
    {0x01, "onDTCStatusChange", "00", "eventType=01h + eventWindowTime + eventTypeRecord"},
    {0x02, "onTimerInterrupt", "00", "eventType=02h + eventWindowTime"},
    {0x03, "onChangeOfDataIdentifier", "00F190", "eventType=03h + DID"},
    {0x04, "reportActivatedEvents", "", "eventType=04h reportActivatedEvents"},
    {0x05, "startResponseOnEvent", "", "eventType=05h startResponseOnEvent"},
    {0x06, "clearResponseOnEvent", "", "eventType=06h clearResponseOnEvent"},
    {0x07, "onComparisonOfValues", "00", "eventType=07h + comparisonRecord"},
};
static const UdsSubFn kSub87[] =
{
    {0x01, "verifyModeTransitionWithFixedBaudrate", "01", "linkControlModel=01h (PC9600)"},
    {0x02, "verifyModeTransitionWithSpecificBaudrate", "0001C200", "linkRecord baudrate"},
    {0x03, "transitionBaudrate", "", "linkControlType=03h transitionBaudrate"},
};

#define UDS_SUB(arr) arr, (int)(sizeof(arr)/sizeof((arr)[0]))

static const UdsServiceDef kIso14229Services[] =
{
    {0x10, "DiagnosticSessionControl", UDS_SUB(kSub10), false, true},
    {0x11, "ECUReset", UDS_SUB(kSub11), false, true},
    {0x14, "ClearDiagnosticInformation", UDS_SUB(kSub14), false, false},
    {0x19, "ReadDTCInformation", UDS_SUB(kSub19), false, true},
    {0x22, "ReadDataByIdentifier", UDS_SUB(kSub22), false, false},
    {0x23, "ReadMemoryByAddress", UDS_SUB(kSub23), false, false},
    {0x24, "ReadScalingDataByIdentifier", UDS_SUB(kSub24), false, false},
    {0x27, "SecurityAccess", UDS_SUB(kSub27), false, true},
    {0x28, "CommunicationControl", UDS_SUB(kSub28), false, true},
    {0x29, "Authentication", UDS_SUB(kSub29), false, true},
    {0x2A, "ReadDataByPeriodicIdentifier", UDS_SUB(kSub2A), true, true},
    {0x2C, "DynamicallyDefineDataIdentifier", UDS_SUB(kSub2C), false, true},
    {0x2E, "WriteDataByIdentifier", UDS_SUB(kSub2E), false, false},
    {0x2F, "InputOutputControlByIdentifier", UDS_SUB(kSub2F), false, false},
    {0x31, "RoutineControl", UDS_SUB(kSub31), false, true},
    {0x34, "RequestDownload", UDS_SUB(kSub34), false, false},
    {0x35, "RequestUpload", UDS_SUB(kSub35), false, false},
    {0x36, "TransferData", UDS_SUB(kSub36), true, false},
    {0x37, "RequestTransferExit", UDS_SUB(kSub37), false, false},
    {0x38, "RequestFileTransfer", UDS_SUB(kSub38), false, true},
    {0x3D, "WriteMemoryByAddress", UDS_SUB(kSub3D), false, false},
    {0x3E, "TesterPresent", UDS_SUB(kSub3E), true, true},
    {0x83, "AccessTimingParameter", UDS_SUB(kSub83), false, true},
    {0x84, "SecuredDataTransmission", UDS_SUB(kSub84), false, false},
    {0x85, "ControlDTCSetting", UDS_SUB(kSub85), false, true},
    {0x86, "ResponseOnEvent", UDS_SUB(kSub86), true, true},
    {0x87, "LinkControl", UDS_SUB(kSub87), false, true},
};

static const int kIso14229ServiceCount = (int)(sizeof(kIso14229Services) / sizeof(kIso14229Services[0]));
