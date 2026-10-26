/* SPDX-License-Identifier: MIT */
/* ARM64 AudioDxe receives no load options in OpenConsole's baseline profile. */
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Library/OcBootManagementLib.h>

EFI_STATUS OcParseLoadOptions(CONST EFI_LOADED_IMAGE_PROTOCOL *LoadedImage,
                              OC_FLEX_ARRAY **ParsedVars) {
  (VOID)LoadedImage;
  if (ParsedVars != NULL) *ParsedVars = NULL;
  return EFI_NOT_FOUND;
}

BOOLEAN OcHasParsedVar(CONST OC_FLEX_ARRAY *ParsedVars, CONST VOID *Name,
                       CONST OC_STRING_FORMAT StringFormat) {
  (VOID)ParsedVars; (VOID)Name; (VOID)StringFormat; return FALSE;
}

EFI_STATUS OcParsedVarsGetInt(CONST OC_FLEX_ARRAY *ParsedVars, CONST VOID *Name,
                              UINTN *Value, CONST OC_STRING_FORMAT StringFormat) {
  (VOID)ParsedVars; (VOID)Name; (VOID)Value; (VOID)StringFormat; return EFI_NOT_FOUND;
}

BOOLEAN OcParsedVarsGetUnicodeStr(CONST OC_FLEX_ARRAY *ParsedVars,
                                  CONST CHAR16 *Name, CHAR16 **StrValue) {
  (VOID)ParsedVars; (VOID)Name; (VOID)StrValue; return FALSE;
}
