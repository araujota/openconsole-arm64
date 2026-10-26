#include <Uefi.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define OC_CRYPTO_PROTOCOL_REVISION 1u
#define OC_CRYPTO_ED448_PUBLIC_KEY_BYTES 57u
#define OC_CRYPTO_ED448_SIGNATURE_BYTES 114u

typedef struct OC_CRYPTO_PROTOCOL OC_CRYPTO_PROTOCOL;
typedef EFI_STATUS(EFIAPI *OC_CRYPTO_SHA256)(OC_CRYPTO_PROTOCOL *,
  CONST UINT8 *, UINTN, UINT8[32]);
typedef EFI_STATUS(EFIAPI *OC_CRYPTO_ED448_VERIFY)(OC_CRYPTO_PROTOCOL *,
  CONST UINT8[OC_CRYPTO_ED448_PUBLIC_KEY_BYTES], CONST UINT8 *, UINTN,
  CONST UINT8[OC_CRYPTO_ED448_SIGNATURE_BYTES]);
struct OC_CRYPTO_PROTOCOL {
  UINT32 Revision;
  UINT32 StructureSize;
  OC_CRYPTO_SHA256 Sha256;
  OC_CRYPTO_ED448_VERIFY Ed448Verify;
};

EFI_GUID gOpenConsoleCryptoProtocolGuid =
  {0x4d5f9e27,0x65f9,0x4a31,{0x93,0x71,0x5e,0x37,0x8a,0x9f,0xd4,0x2b}};
STATIC EFI_HANDLE mProtocolHandle;

STATIC EFI_STATUS EFIAPI OcSha256(OC_CRYPTO_PROTOCOL *This,
  CONST UINT8 *Data, UINTN DataSize, UINT8 Digest[32]) {
  (VOID)This;
  if ((Data == NULL && DataSize != 0) || Digest == NULL) return EFI_INVALID_PARAMETER;
  return Sha256HashAll(Data, DataSize, Digest) ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

STATIC EFI_STATUS EFIAPI OcEd448Verify(OC_CRYPTO_PROTOCOL *This,
  CONST UINT8 PublicKey[OC_CRYPTO_ED448_PUBLIC_KEY_BYTES], CONST UINT8 *Message,
  UINTN MessageSize, CONST UINT8 Signature[OC_CRYPTO_ED448_SIGNATURE_BYTES]) {
  VOID *Context;
  BOOLEAN Valid;
  (VOID)This;
  if (PublicKey == NULL || Message == NULL || MessageSize == 0 || Signature == NULL)
    return EFI_INVALID_PARAMETER;
  Context = EdDsaNewByNid(CRYPTO_NID_ED448);
  if (Context == NULL) return EFI_OUT_OF_RESOURCES;
  if (!EdDsaSetPubKey(Context, PublicKey, OC_CRYPTO_ED448_PUBLIC_KEY_BYTES)) {
    EdDsaFree(Context);
    return EFI_INVALID_PARAMETER;
  }
  Valid = EdDsaVerify(Context, NULL, 0, Message, MessageSize,
                      (UINT8 *)(UINTN)Signature, OC_CRYPTO_ED448_SIGNATURE_BYTES);
  EdDsaFree(Context);
  return Valid ? EFI_SUCCESS : EFI_SECURITY_VIOLATION;
}

STATIC OC_CRYPTO_PROTOCOL mProtocol = {
  OC_CRYPTO_PROTOCOL_REVISION, sizeof(OC_CRYPTO_PROTOCOL), OcSha256, OcEd448Verify
};

EFI_STATUS EFIAPI OpenConsoleCryptoEntryPoint(EFI_HANDLE ImageHandle,
  EFI_SYSTEM_TABLE *SystemTable) {
  (VOID)ImageHandle; (VOID)SystemTable;
  mProtocolHandle = NULL;
  return gBS->InstallProtocolInterface(&mProtocolHandle,
    &gOpenConsoleCryptoProtocolGuid, EFI_NATIVE_INTERFACE, &mProtocol);
}

EFI_STATUS EFIAPI OpenConsoleCryptoUnload(EFI_HANDLE ImageHandle) {
  (VOID)ImageHandle;
  if (mProtocolHandle == NULL) return EFI_SUCCESS;
  return gBS->UninstallProtocolInterface(mProtocolHandle,
    &gOpenConsoleCryptoProtocolGuid, &mProtocol);
}
