/** @file

  This driver produces Block I/O Protocol instances for virtio-blk devices.

  The implementation is basic:

  - No attach/detach (ie. removable media).

  - Although the non-blocking interfaces of EFI_BLOCK_IO2_PROTOCOL could be a
    good match for multiple in-flight virtio-blk requests, we stick to
    synchronous requests and EFI_BLOCK_IO_PROTOCOL for now.

  Copyright (C) 2012, Red Hat, Inc.

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License which accompanies this
  distribution. The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <IndustryStandard/Pci.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "VirtioBlk.h"

/**

  Write a word into Region 0 of the device specified by PciIo.

  Region 0 must be an iomem region. This is an internal function for the
  VIRTIO_CFG_WRITE() macro below.

  @param[in] PciIo        Target PCI device.

  @param[in] FieldOffset  Destination offset.

  @param[in] FieldSize    Destination field size, must be in { 1, 2, 4, 8 }.

  @param[in] Value        Little endian value to write, converted to UINT64.
                          The least significant FieldSize bytes will be used.


  @return  Status code returned by PciIo->Io.Write().

**/
STATIC
EFIAPI
EFI_STATUS
VirtioWrite (
  IN EFI_PCI_IO_PROTOCOL *PciIo,
  IN UINTN               FieldOffset,
  IN UINTN               FieldSize,
  IN UINT64              Value
  )
{
  UINTN                     Count;
  EFI_PCI_IO_PROTOCOL_WIDTH Width;

  Count = 1;
  switch (FieldSize) {
    case 1:
      Width = EfiPciIoWidthUint8;
      break;

    case 2:
      Width = EfiPciIoWidthUint16;
      break;

    case 8:
      Count = 2;
      // fall through

    case 4:
      Width = EfiPciIoWidthUint32;
      break;

    default:
      ASSERT (FALSE);
  }

  return PciIo->Io.Write (
                     PciIo,
                     Width,
                     PCI_BAR_IDX0,
                     FieldOffset,
                     Count,
                     &Value
                     );
}


/**

  Read a word from Region 0 of the device specified by PciIo.

  Region 0 must be an iomem region. This is an internal function for the
  VIRTIO_CFG_READ() macro below.

  @param[in] PciIo        Source PCI device.

  @param[in] FieldOffset  Source offset.

  @param[in] FieldSize    Source field size, must be in { 1, 2, 4, 8 }.

  @param[in] BufferSize   Number of bytes available in the target buffer. Must
                          equal FieldSize.

  @param[out] Buffer      Target buffer.


  @return  Status code returned by PciIo->Io.Read().

**/
STATIC
EFIAPI
EFI_STATUS
VirtioRead (
  IN  EFI_PCI_IO_PROTOCOL *PciIo,
  IN  UINTN               FieldOffset,
  IN  UINTN               FieldSize,
  IN  UINTN               BufferSize,
  OUT VOID                *Buffer
  )
{
  UINTN                     Count;
  EFI_PCI_IO_PROTOCOL_WIDTH Width;

  ASSERT (FieldSize == BufferSize);

  Count = 1;
  switch (FieldSize) {
    case 1:
      Width = EfiPciIoWidthUint8;
      break;

    case 2:
      Width = EfiPciIoWidthUint16;
      break;

    case 8:
      Count = 2;
      // fall through

    case 4:
      Width = EfiPciIoWidthUint32;
      break;

    default:
      ASSERT (FALSE);
  }

  return PciIo->Io.Read (
                     PciIo,
                     Width,
                     PCI_BAR_IDX0,
                     FieldOffset,
                     Count,
                     Buffer
                     );
}


/**

  Convenience macros to read and write region 0 IO space elements of the
  virtio-blk PCI device, for configuration purposes.

  The following macros make it possible to specify only the "core parameters"
  for such accesses and to derive the rest. By the time VIRTIO_CFG_WRITE()
  returns, the transaction will have been completed.

  @param[in] Dev       Pointer to the VBLK_DEV structure whose PCI IO space
                       we're accessing. Dev->PciIo must be valid.

  @param[in] Field     A field name from VBLK_HDR, identifying the virtio-blk
                       configuration item to access.

  @param[in] Value     (VIRTIO_CFG_WRITE() only.) The value to write to the
                       selected configuration item.

  @param[out] Pointer  (VIRTIO_CFG_READ() only.) The object to receive the
                       value read from the configuration item. Its type must be
                       one of UINT8, UINT16, UINT32, UINT64.


  @return  Status code returned by VirtioWrite() / VirtioRead().

**/

#define VIRTIO_CFG_WRITE(Dev, Field, Value)  (VirtioWrite (             \
                                                (Dev)->PciIo,           \
                                                OFFSET_OF_VHDR (Field), \
                                                SIZE_OF_VHDR (Field),   \
                                                (Value)                 \
                                                ))

#define VIRTIO_CFG_READ(Dev, Field, Pointer) (VirtioRead (              \
                                                (Dev)->PciIo,           \
                                                OFFSET_OF_VHDR (Field), \
                                                SIZE_OF_VHDR (Field),   \
                                                sizeof *(Pointer),      \
                                                (Pointer)               \
                                                ))


//
// UEFI Spec 2.3.1 + Errata C, 12.8 EFI Block I/O Protocol
// Driver Writer's Guide for UEFI 2.3.1 v1.01,
//   24.2 Block I/O Protocol Implementations
//
EFI_STATUS
EFIAPI
VirtioBlkReset (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN BOOLEAN               ExtendedVerification
  )
{
  //
  // If we managed to initialize and install the driver, then the device is
  // working correctly.
  //
  return EFI_SUCCESS;
}

/**

  Verify correctness of the read/write (not flush) request submitted to the
  EFI_BLOCK_IO_PROTOCOL instance.

  This function provides most verification steps described in:

    UEFI Spec 2.3.1 + Errata C, 12.8 EFI Block I/O Protocol, 12.8 EFI Block I/O
    Protocol,
    - EFI_BLOCK_IO_PROTOCOL.ReadBlocks()
    - EFI_BLOCK_IO_PROTOCOL.WriteBlocks()

    Driver Writer's Guide for UEFI 2.3.1 v1.01,
    - 24.2.2. ReadBlocks() and ReadBlocksEx() Implementation
    - 24.2.3 WriteBlocks() and WriteBlockEx() Implementation

  Request sizes are limited to 1 GB (checked). This is not a practical
  limitation, just conformance to virtio-0.9.5, 2.3.2 Descriptor Table: "no
  descriptor chain may be more than 2^32 bytes long in total".

  Some Media characteristics are hardcoded in VirtioBlkInit() below (like
  non-removable media, no restriction on buffer alignment etc); we rely on
  those here without explicit mention.

  @param[in] Media               The EFI_BLOCK_IO_MEDIA characteristics for
                                 this driver instance, extracted from the
                                 underlying virtio-blk device at initialization
                                 time. We validate the request against this set
                                 of attributes.


  @param[in] Lba                 Logical Block Address: number of logical
                                 blocks to skip from the beginning of the
                                 device.

  @param[in] PositiveBufferSize  Size of buffer to transfer, in bytes. The
                                 caller is responsible to ensure this parameter
                                 is positive.

  @param[in] RequestIsWrite      TRUE iff data transfer goes from guest to
                                 device.


  @@return                       Validation result to be forwarded outwards by
                                 ReadBlocks() and WriteBlocks, as required by
                                 the specs above.

**/
STATIC
EFI_STATUS
EFIAPI
VerifyReadWriteRequest (
  IN  EFI_BLOCK_IO_MEDIA *Media,
  IN  EFI_LBA            Lba,
  IN  UINTN              PositiveBufferSize,
  IN  BOOLEAN            RequestIsWrite
  )
{
  UINTN BlockCount;

  ASSERT (PositiveBufferSize > 0);

  if (PositiveBufferSize > SIZE_1GB ||
      PositiveBufferSize % Media->BlockSize > 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  BlockCount = PositiveBufferSize / Media->BlockSize;

  //
  // Avoid unsigned wraparound on either side in the second comparison.
  //
  if (Lba > Media->LastBlock || BlockCount - 1 > Media->LastBlock - Lba) {
    return EFI_INVALID_PARAMETER;
  }

  if (RequestIsWrite && Media->ReadOnly) {
    return EFI_WRITE_PROTECTED;
  }

  return EFI_SUCCESS;
}


/**

  Append a contiguous buffer for transmission / reception via the virtio ring.

  This function implements the following sections from virtio-0.9.5:
  - 2.4.1.1 Placing Buffers into the Descriptor Table
  - 2.4.1.2 Updating the Available Ring

  Free space is taken as granted, since this driver supports only synchronous
  requests and host side status is processed in lock-step with request
  submission. VirtioBlkInit() verifies the ring size in advance.

  @param[in out] Ring           The virtio ring to append the buffer to, as a
                                descriptor.

  @param [in] BufferPhysAddr    (Guest pseudo-physical) start address of the
                                transmit / receive buffer

  @param [in] BufferSize        Number of bytes to transmit or receive.

  @param [in] Flags             A bitmask of VRING_DESC_F_* flags. The caller
                                computes this mask dependent on further buffers
                                to append and transfer direction.
                                VRING_DESC_F_INDIRECT is unsupported. The
                                VRING_DESC.Next field is always set, but the
                                host only interprets it dependent on
                                VRING_DESC_F_NEXT.

  @param [in] HeadIdx           The index identifying the head buffer (first
                                buffer appended) belonging to this same
                                request.

  @param [in out] NextAvailIdx  On input, the index identifying the next
                                descriptor available to carry the buffer. On
                                output, incremented by one, modulo 2^16.

**/

STATIC
VOID
EFIAPI
AppendDesc (
  IN OUT          VRING  *Ring,
  IN              UINTN  BufferPhysAddr,
  IN              UINT32 BufferSize,
  IN              UINT16 Flags,
  IN              UINT16 HeadIdx,
  IN OUT          UINT16 *NextAvailIdx
  )
{
  volatile VRING_DESC *Desc;

  Desc        = &Ring->Desc[*NextAvailIdx % Ring->QueueSize];
  Desc->Addr  = BufferPhysAddr;
  Desc->Len   = BufferSize;
  Desc->Flags = Flags;
  Ring->Avail.Ring[(*NextAvailIdx)++ % Ring->QueueSize] =
    HeadIdx % Ring->QueueSize;
  Desc->Next  = *NextAvailIdx % Ring->QueueSize;
}


/**

  Format a read / write / flush request as three consecutive virtio
  descriptors, push them to the host, and poll for the response.

  This is the main workhorse function. Two use cases are supported, read/write
  and flush. The function may only be called after the request parameters have
  been verified by
  - specific checks in ReadBlocks() / WriteBlocks() / FlushBlocks(), and
  - VerifyReadWriteRequest() (for read/write only).

  Parameters handled commonly:

    @param[in] Dev             The virtio-blk device the request is targeted
                               at.

  Flush request:

    @param[in] Lba             Must be zero.

    @param[in] BufferSize      Must be zero.

    @param[in out] Buffer      Ignored by the function.

    @param[in] RequestIsWrite  Must be TRUE.

  Read/Write request:

    @param[in] Lba             Logical Block Address: number of logical blocks
                               to skip from the beginning of the device.

    @param[in] BufferSize      Size of buffer to transfer, in bytes. The caller
                               is responsible to ensure this parameter is
                               positive.

    @param[in out] Buffer      The guest side area to read data from the device
                               into, or write data to the device from.

    @param[in] RequestIsWrite  TRUE iff data transfer goes from guest to
                               device.

  Return values are common to both use cases, and are appropriate to be
  forwarded by the EFI_BLOCK_IO_PROTOCOL functions (ReadBlocks(),
  WriteBlocks(), FlushBlocks()).


  @retval EFI_SUCCESS          Transfer complete.

  @retval EFI_DEVICE_ERROR     Failed to notify host side via PCI write, or
                               unable to parse host response, or host response
                               is not VIRTIO_BLK_S_OK.

**/

STATIC
EFI_STATUS
EFIAPI
SynchronousRequest (
  IN              VBLK_DEV *Dev,
  IN              EFI_LBA  Lba,
  IN              UINTN    BufferSize,
  IN OUT volatile VOID     *Buffer,
  IN              BOOLEAN  RequestIsWrite
  )
{
  UINT32                  BlockSize;
  volatile VIRTIO_BLK_REQ Request;
  volatile UINT8          HostStatus;
  UINT16                  FirstAvailIdx;
  UINT16                  NextAvailIdx;
  UINTN                   PollPeriodUsecs;

  BlockSize = Dev->BlockIoMedia.BlockSize;

  //
  // ensured by VirtioBlkInit()
  //
  ASSERT (BlockSize > 0);
  ASSERT (BlockSize % 512 == 0);

  //
  // ensured by contract above, plus VerifyReadWriteRequest()
  //
  ASSERT (BufferSize % BlockSize == 0);

  //
  // Prepare virtio-blk request header, setting zero size for flush.
  // IO Priority is homogeneously 0.
  //
  Request.Type   = RequestIsWrite ?
                   (BufferSize == 0 ? VIRTIO_BLK_T_FLUSH : VIRTIO_BLK_T_OUT) :
                   VIRTIO_BLK_T_IN;
  Request.IoPrio = 0;
  Request.Sector = Lba * (BlockSize / 512);

  //
  // Prepare for virtio-0.9.5, 2.4.2 Receiving Used Buffers From the Device.
  // We're going to poll the answer, the host should not send an interrupt.
  //
  *Dev->Ring.Avail.Flags = (UINT16) VRING_AVAIL_F_NO_INTERRUPT;

  //
  // preset a host status for ourselves that we do not accept as success
  //
  HostStatus = VIRTIO_BLK_S_IOERR;

  //
  // ensured by VirtioBlkInit() -- this predicate, in combination with the
  // lock-step progress, ensures we don't have to track free descriptors.
  //
  ASSERT (Dev->Ring.QueueSize >= 3);

  //
  // Implement virtio-0.9.5, 2.4.1 Supplying Buffers to the Device.
  //
  FirstAvailIdx = *Dev->Ring.Avail.Idx;
  NextAvailIdx  = FirstAvailIdx;

  //
  // virtio-blk header in first desc
  //
  AppendDesc (&Dev->Ring, (UINTN) &Request, sizeof Request, VRING_DESC_F_NEXT,
    FirstAvailIdx, &NextAvailIdx);

  //
  // data buffer for read/write in second desc
  //
  if (BufferSize > 0) {
    //
    // From virtio-0.9.5, 2.3.2 Descriptor Table:
    // "no descriptor chain may be more than 2^32 bytes long in total".
    //
    // The predicate is ensured by the call contract above (for flush), or
    // VerifyReadWriteRequest() (for read/write). It also implies that
    // converting BufferSize to UINT32 will not truncate it.
    //
    ASSERT (BufferSize <= SIZE_1GB);

    //
    // VRING_DESC_F_WRITE is interpreted from the host's point of view.
    //
    AppendDesc (&Dev->Ring, (UINTN) Buffer, (UINT32) BufferSize,
      VRING_DESC_F_NEXT | (RequestIsWrite ? 0 : VRING_DESC_F_WRITE),
      FirstAvailIdx, &NextAvailIdx);
  }

  //
  // host status in last (second or third) desc
  //
  AppendDesc (&Dev->Ring, (UINTN) &HostStatus, sizeof HostStatus,
    VRING_DESC_F_WRITE, FirstAvailIdx, &NextAvailIdx);

  //
  // virtio-0.9.5, 2.4.1.3 Updating the Index Field
  //
  MemoryFence();
  *Dev->Ring.Avail.Idx = NextAvailIdx;

  //
  // virtio-0.9.5, 2.4.1.4 Notifying the Device -- gratuitous notifications are
  // OK. virtio-blk's only virtqueue is #0, called "requestq" (see Appendix D).
  //
  MemoryFence();
  if (EFI_ERROR (VIRTIO_CFG_WRITE (Dev, Generic.VhdrQueueNotify, 0))) {
    return EFI_DEVICE_ERROR;
  }

  //
  // virtio-0.9.5, 2.4.2 Receiving Used Buffers From the Device
  // Wait until the host processes and acknowledges our 3-part descriptor
  // chain. The condition we use for polling is greatly simplified and relies
  // on synchronous, the lock-step progress.
  //
  // Keep slowing down until we reach a poll period of slightly above 1 ms.
  //
  PollPeriodUsecs = 1;
  MemoryFence();
  while (*Dev->Ring.Used.Idx != NextAvailIdx) {
    gBS->Stall (PollPeriodUsecs); // calls AcpiTimerLib::MicroSecondDelay

    if (PollPeriodUsecs < 1024) {
      PollPeriodUsecs *= 2;
    }
    MemoryFence();
  }

  if (HostStatus == VIRTIO_BLK_S_OK) {
    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}


/**

  ReadBlocks() operation for virtio-blk.

  See
  - UEFI Spec 2.3.1 + Errata C, 12.8 EFI Block I/O Protocol, 12.8 EFI Block I/O
    Protocol, EFI_BLOCK_IO_PROTOCOL.ReadBlocks().
  - Driver Writer's Guide for UEFI 2.3.1 v1.01, 24.2.2. ReadBlocks() and
    ReadBlocksEx() Implementation.

  Parameter checks and conformant return values are implemented in
  VerifyReadWriteRequest() and SynchronousRequest().

  A zero BufferSize doesn't seem to be prohibited, so do nothing in that case,
  successfully.

**/

EFI_STATUS
EFIAPI
VirtioBlkReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL *This,
  IN  UINT32                MediaId,
  IN  EFI_LBA               Lba,
  IN  UINTN                 BufferSize,
  OUT VOID                  *Buffer
  )
{
  VBLK_DEV   *Dev;
  EFI_STATUS Status;

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }

  Dev = VIRTIO_BLK_FROM_BLOCK_IO (This);
  Status = VerifyReadWriteRequest (
             &Dev->BlockIoMedia,
             Lba,
             BufferSize,
             FALSE               // RequestIsWrite
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return SynchronousRequest (
           Dev,
           Lba,
           BufferSize,
           Buffer,
           FALSE       // RequestIsWrite
           );
}

/**

  WriteBlocks() operation for virtio-blk.

  See
  - UEFI Spec 2.3.1 + Errata C, 12.8 EFI Block I/O Protocol, 12.8 EFI Block I/O
    Protocol, EFI_BLOCK_IO_PROTOCOL.WriteBlocks().
  - Driver Writer's Guide for UEFI 2.3.1 v1.01, 24.2.3 WriteBlocks() and
    WriteBlockEx() Implementation.

  Parameter checks and conformant return values are implemented in
  VerifyReadWriteRequest() and SynchronousRequest().

  A zero BufferSize doesn't seem to be prohibited, so do nothing in that case,
  successfully.

**/

EFI_STATUS
EFIAPI
VirtioBlkWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This,
  IN UINT32                MediaId,
  IN EFI_LBA               Lba,
  IN UINTN                 BufferSize,
  IN VOID                  *Buffer
  )
{
  VBLK_DEV   *Dev;
  EFI_STATUS Status;

  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }

  Dev = VIRTIO_BLK_FROM_BLOCK_IO (This);
  Status = VerifyReadWriteRequest (
             &Dev->BlockIoMedia,
             Lba,
             BufferSize,
             TRUE                // RequestIsWrite
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return SynchronousRequest (
           Dev,
           Lba,
           BufferSize,
           Buffer,
           TRUE        // RequestIsWrite
           );
}


/**

  FlushBlocks() operation for virtio-blk.

  See
  - UEFI Spec 2.3.1 + Errata C, 12.8 EFI Block I/O Protocol, 12.8 EFI Block I/O
    Protocol, EFI_BLOCK_IO_PROTOCOL.FlushBlocks().
  - Driver Writer's Guide for UEFI 2.3.1 v1.01, 24.2.4 FlushBlocks() and
    FlushBlocksEx() Implementation.

  If the underlying virtio-blk device doesn't support flushing (ie.
  write-caching), then this function should not be called by higher layers,
  according to EFI_BLOCK_IO_MEDIA characteristics set in VirtioBlkInit().
  Should they do nonetheless, we do nothing, successfully.

**/

EFI_STATUS
EFIAPI
VirtioBlkFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL *This
  )
{
  VBLK_DEV *Dev;

  Dev = VIRTIO_BLK_FROM_BLOCK_IO (This);
  return Dev->BlockIoMedia.WriteCaching ?
           SynchronousRequest (
             Dev,
             0,    // Lba
             0,    // BufferSize
             NULL, // Buffer
             TRUE  // RequestIsWrite
             ) :
           EFI_SUCCESS;
}


/**

  Device probe function for this driver.

  The DXE core calls this function for any given device in order to see if the
  driver can drive the device.

  Specs relevant in the general sense:

  - UEFI Spec 2.3.1 + Errata C:
    - 6.3 Protocol Handler Services -- for accessing the underlying device
    - 10.1 EFI Driver Binding Protocol -- for exporting ourselves

  - Driver Writer's Guide for UEFI 2.3.1 v1.01:
    - 5.1.3.4 OpenProtocol() and CloseProtocol() -- for accessing the
      underlying device
    - 9 Driver Binding Protocol -- for exporting ourselves

  Specs relevant in the specific sense:
  - UEFI Spec 2.3.1 + Errata C, 13.4 EFI PCI I/O Protocol
  - Driver Writer's Guide for UEFI 2.3.1 v1.01, 18 PCI Driver Design
    Guidelines, 18.3 PCI drivers.

  @param[in]  This                The EFI_DRIVER_BINDING_PROTOCOL object
                                  incorporating this driver (independently of
                                  any device).

  @param[in] DeviceHandle         The device to probe.

  @param[in] RemainingDevicePath  Relevant only for bus drivers, ignored.


  @retval EFI_SUCCESS      The driver supports the device being probed.

  @retval EFI_UNSUPPORTED  Based on virtio-blk PCI discovery, we do not support
                           the device.

  @return                  Error codes from the OpenProtocol() boot service or
                           the PciIo protocol.

**/

EFI_STATUS
EFIAPI
VirtioBlkDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  DeviceHandle,
  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath
  )
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;

  //
  // Attempt to open the device with the PciIo set of interfaces. On success,
  // the protocol is "instantiated" for the PCI device. Covers duplicate open
  // attempts (EFI_ALREADY_STARTED).
  //
  Status = gBS->OpenProtocol (
                  DeviceHandle,               // candidate device
                  &gEfiPciIoProtocolGuid,     // for generic PCI access
                  (VOID **)&PciIo,            // handle to instantiate
                  This->DriverBindingHandle,  // requestor driver identity
                  DeviceHandle,               // ControllerHandle, according to
                                              // the UEFI Driver Model
                  EFI_OPEN_PROTOCOL_BY_DRIVER // get exclusive PciIo access to
                                              // the device; to be released
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Read entire PCI configuration header for more extensive check ahead.
  //
  Status = PciIo->Pci.Read (
                        PciIo,                        // (protocol, device)
                                                      // handle
                        EfiPciIoWidthUint32,          // access width & copy
                                                      // mode
                        0,                            // Offset
                        sizeof Pci / sizeof (UINT32), // Count
                        &Pci                          // target buffer
                        );

  if (Status == EFI_SUCCESS) {
    //
    // virtio-0.9.5, 2.1 PCI Discovery
    //
    Status = (Pci.Hdr.VendorId == 0x1AF4 &&
              Pci.Hdr.DeviceId >= 0x1000 && Pci.Hdr.DeviceId <= 0x103F &&
              Pci.Hdr.RevisionID == 0x00 &&
              Pci.Device.SubsystemID == 0x02) ? EFI_SUCCESS : EFI_UNSUPPORTED;
  }

  //
  // We needed PCI IO access only transitorily, to see whether we support the
  // device or not.
  //
  gBS->CloseProtocol (DeviceHandle, &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle, DeviceHandle);
  return Status;
}


/**

  Configure a virtio ring.

  This function sets up internal storage (the guest-host communication area)
  and lays out several "navigation" (ie. no-ownership) pointers to parts of
  that storage.

  Relevant sections from the virtio-0.9.5 spec:
  - 1.1 Virtqueues,
  - 2.3 Virtqueue Configuration.

  @param[in]                    The number of descriptors to allocate for the
                                virtio ring, as requested by the host.

  @param[out] Ring              The virtio ring to set up.

  @retval EFI_OUT_OF_RESOURCES  AllocatePages() failed to allocate contiguous
                                pages for the requested QueueSize. Fields of
                                Ring have indeterminate value.

  @retval EFI_SUCCESS           Allocation and setup successful. Ring->Base
                                (and nothing else) is responsible for
                                deallocation.

**/

STATIC
EFI_STATUS
EFIAPI
VirtioRingInit (
  IN  UINT16 QueueSize,
  OUT VRING  *Ring
  )
{
  UINTN          RingSize;
  volatile UINT8 *RingPagesPtr;

  RingSize = ALIGN_VALUE (
               sizeof *Ring->Desc            * QueueSize +
               sizeof *Ring->Avail.Flags                 +
               sizeof *Ring->Avail.Idx                   +
               sizeof *Ring->Avail.Ring      * QueueSize +
               sizeof *Ring->Avail.UsedEvent,
               EFI_PAGE_SIZE);

  RingSize += ALIGN_VALUE (
                sizeof *Ring->Used.Flags                  +
                sizeof *Ring->Used.Idx                    +
                sizeof *Ring->Used.UsedElem   * QueueSize +
                sizeof *Ring->Used.AvailEvent,
                EFI_PAGE_SIZE);

  Ring->NumPages = EFI_SIZE_TO_PAGES (RingSize);
  Ring->Base = AllocatePages (Ring->NumPages);
  if (Ring->Base == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  SetMem (Ring->Base, RingSize, 0x00);
  RingPagesPtr = Ring->Base;

  Ring->Desc = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Desc * QueueSize;

  Ring->Avail.Flags = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Flags;

  Ring->Avail.Idx = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Idx;

  Ring->Avail.Ring = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.Ring * QueueSize;

  Ring->Avail.UsedEvent = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Avail.UsedEvent;

  RingPagesPtr = (volatile UINT8 *) Ring->Base +
                 ALIGN_VALUE (RingPagesPtr - (volatile UINT8 *) Ring->Base,
                   EFI_PAGE_SIZE);

  Ring->Used.Flags = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.Flags;

  Ring->Used.Idx = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.Idx;

  Ring->Used.UsedElem = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.UsedElem * QueueSize;

  Ring->Used.AvailEvent = (volatile VOID *) RingPagesPtr;
  RingPagesPtr += sizeof *Ring->Used.AvailEvent;

  Ring->QueueSize = QueueSize;
  return EFI_SUCCESS;
}


/**

  Tear down the internal resources of a configured virtio ring.

  The caller is responsible to stop the host from using this ring before
  invoking this function: the VSTAT_DRIVER_OK bit must be clear in
  VhdrDeviceStatus.

  @param[out] Ring  The virtio ring to clean up.

**/


STATIC
VOID
EFIAPI
VirtioRingUninit (
  IN OUT VRING *Ring
  )
{
  FreePages (Ring->Base, Ring->NumPages);
  SetMem (Ring, sizeof *Ring, 0x00);
}


/**

  Set up all BlockIo and virtio-blk aspects of this driver for the specified
  device.

  @param[in out] Dev  The driver instance to configure. The caller is
                      responsible for Dev->PciIo's validity (ie. working IO
                      access to the underlying virtio-blk PCI device).

  @retval EFI_SUCCESS      Setup complete.

  @retval EFI_UNSUPPORTED  The driver is unable to work with the virtio ring or
                           virtio-blk attributes the host provides.

  @return                  Error codes from VirtioRingInit() or
                           VIRTIO_CFG_READ() / VIRTIO_CFG_WRITE().

**/

STATIC
EFI_STATUS
EFIAPI
VirtioBlkInit (
  IN OUT VBLK_DEV *Dev
  )
{
  UINT8      NextDevStat;
  EFI_STATUS Status;

  UINT32     Features;
  UINT64     NumSectors;
  UINT32     BlockSize;
  UINT16     QueueSize;

  //
  // Execute virtio-0.9.5, 2.2.1 Device Initialization Sequence.
  //
  NextDevStat = 0;             // step 1 -- reset device
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, NextDevStat);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  NextDevStat |= VSTAT_ACK;    // step 2 -- acknowledge device presence
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, NextDevStat);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  NextDevStat |= VSTAT_DRIVER; // step 3 -- we know how to drive it
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, NextDevStat);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  //
  // step 4a -- retrieve and validate features
  //
  Status = VIRTIO_CFG_READ (Dev, Generic.VhdrDeviceFeatureBits, &Features);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }
  Status = VIRTIO_CFG_READ (Dev, VhdrCapacity, &NumSectors);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }
  if (NumSectors == 0) {
    Status = EFI_UNSUPPORTED;
    goto Failed;
  }

  if (Features & VIRTIO_BLK_F_BLK_SIZE) {
    Status = VIRTIO_CFG_READ (Dev, VhdrBlkSize, &BlockSize);
    if (EFI_ERROR (Status)) {
      goto Failed;
    }
    if (BlockSize == 0 || BlockSize % 512 != 0 ||
        NumSectors % (BlockSize / 512) != 0) {
      //
      // We can only handle a logical block consisting of whole sectors,
      // and only a disk composed of whole logical blocks.
      //
      Status = EFI_UNSUPPORTED;
      goto Failed;
    }
  }
  else {
    BlockSize = 512;
  }

  //
  // step 4b -- allocate virtqueue
  //
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrQueueSelect, 0);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }
  Status = VIRTIO_CFG_READ (Dev, Generic.VhdrQueueSize, &QueueSize);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }
  if (QueueSize < 3) { // SynchronousRequest() uses at most three descriptors
    Status = EFI_UNSUPPORTED;
    goto Failed;
  }

  Status = VirtioRingInit (QueueSize, &Dev->Ring);
  if (EFI_ERROR (Status)) {
    goto Failed;
  }

  //
  // step 4c -- Report GPFN (guest-physical frame number) of queue. If anything
  // fails from here on, we must release the ring resources.
  //
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrQueueAddress,
             (UINTN) Dev->Ring.Base >> EFI_PAGE_SHIFT);
  if (EFI_ERROR (Status)) {
    goto ReleaseQueue;
  }

  //
  // step 5 -- Report understood features. There are no virtio-blk specific
  // features to negotiate in virtio-0.9.5, plus we do not want any of the
  // device-independent (known or unknown) VIRTIO_F_* capabilities (see
  // Appendix B).
  //
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrGuestFeatureBits, 0);
  if (EFI_ERROR (Status)) {
    goto ReleaseQueue;
  }

  //
  // step 6 -- initialization complete
  //
  NextDevStat |= VSTAT_DRIVER_OK;
  Status = VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, NextDevStat);
  if (EFI_ERROR (Status)) {
    goto ReleaseQueue;
  }

  //
  // Populate the exported interface's attributes; see UEFI spec v2.3.1 +
  // Errata C, 12.8 EFI Block I/O Protocol. We stick to the lowest possible
  // EFI_BLOCK_IO_PROTOCOL revision for now.
  //
  Dev->BlockIo.Revision              = 0;
  Dev->BlockIo.Media                 = &Dev->BlockIoMedia;
  Dev->BlockIo.Reset                 = &VirtioBlkReset;
  Dev->BlockIo.ReadBlocks            = &VirtioBlkReadBlocks;
  Dev->BlockIo.WriteBlocks           = &VirtioBlkWriteBlocks;
  Dev->BlockIo.FlushBlocks           = &VirtioBlkFlushBlocks;
  Dev->BlockIoMedia.MediaId          = 0;
  Dev->BlockIoMedia.RemovableMedia   = FALSE;
  Dev->BlockIoMedia.MediaPresent     = TRUE;
  Dev->BlockIoMedia.LogicalPartition = FALSE;
  Dev->BlockIoMedia.ReadOnly         = !!(Features & VIRTIO_BLK_F_RO);
  Dev->BlockIoMedia.WriteCaching     = !!(Features & VIRTIO_BLK_F_FLUSH);
  Dev->BlockIoMedia.BlockSize        = BlockSize;
  Dev->BlockIoMedia.IoAlign          = 0;
  Dev->BlockIoMedia.LastBlock        = NumSectors / (BlockSize / 512) - 1;
  return EFI_SUCCESS;

ReleaseQueue:
  VirtioRingUninit (&Dev->Ring);

Failed:
  //
  // Notify the host about our failure to setup: virtio-0.9.5, 2.2.2.1 Device
  // Status. PCI IO access failure here should not mask the original error.
  //
  NextDevStat |= VSTAT_FAILED;
  VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, NextDevStat);

  return Status; // reached only via Failed above
}


/**

  Uninitialize the internals of a virtio-blk device that has been successfully
  set up with VirtioBlkInit().

  @param[in out]  Dev  The device to clean up.

**/

STATIC
VOID
EFIAPI
VirtioBlkUninit (
  IN OUT VBLK_DEV *Dev
  )
{
  //
  // Reset the virtual device -- see virtio-0.9.5, 2.2.2.1 Device Status. When
  // VIRTIO_CFG_WRITE() returns, the host will have learned to stay away from
  // the old comms area.
  //
  VIRTIO_CFG_WRITE (Dev, Generic.VhdrDeviceStatus, 0);

  VirtioRingUninit (&Dev->Ring);

  SetMem (&Dev->BlockIo,      sizeof Dev->BlockIo,      0x00);
  SetMem (&Dev->BlockIoMedia, sizeof Dev->BlockIoMedia, 0x00);
}


/**

  After we've pronounced support for a specific device in
  DriverBindingSupported(), we start managing said device (passed in by the
  Driver Exeuction Environment) with the following service.

  See DriverBindingSupported() for specification references.

  @param[in]  This                The EFI_DRIVER_BINDING_PROTOCOL object
                                  incorporating this driver (independently of
                                  any device).

  @param[in] DeviceHandle         The supported device to drive.

  @param[in] RemainingDevicePath  Relevant only for bus drivers, ignored.


  @retval EFI_SUCCESS           Driver instance has been created and
                                initialized  for the virtio-blk PCI device, it
                                is now accessibla via EFI_BLOCK_IO_PROTOCOL.

  @retval EFI_OUT_OF_RESOURCES  Memory allocation failed.

  @return                       Error codes from the OpenProtocol() boot
                                service, the PciIo protocol, VirtioBlkInit(),
                                or the InstallProtocolInterface() boot service.

**/

EFI_STATUS
EFIAPI
VirtioBlkDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  DeviceHandle,
  IN EFI_DEVICE_PATH_PROTOCOL    *RemainingDevicePath
  )
{
  VBLK_DEV   *Dev;
  EFI_STATUS Status;

  Dev = (VBLK_DEV *) AllocateZeroPool (sizeof *Dev);
  if (Dev == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->OpenProtocol (DeviceHandle, &gEfiPciIoProtocolGuid,
                  (VOID **)&Dev->PciIo, This->DriverBindingHandle,
                  DeviceHandle, EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
    goto FreeVirtioBlk;
  }

  //
  // We must retain and ultimately restore the original PCI attributes of the
  // device. See Driver Writer's Guide for UEFI 2.3.1 v1.01, 18.3 PCI drivers /
  // 18.3.2 Start() and Stop().
  //
  // The third parameter ("Attributes", input) is ignored by the Get operation.
  // The fourth parameter ("Result", output) is ignored by the Enable and Set
  // operations.
  //
  // For virtio-blk we only need IO space access.
  //
  Status = Dev->PciIo->Attributes (Dev->PciIo, EfiPciIoAttributeOperationGet,
                         0, &Dev->OriginalPciAttributes);
  if (EFI_ERROR (Status)) {
    goto ClosePciIo;
  }

  Status = Dev->PciIo->Attributes (Dev->PciIo,
                         EfiPciIoAttributeOperationEnable,
                         EFI_PCI_IO_ATTRIBUTE_IO, NULL);
  if (EFI_ERROR (Status)) {
    goto ClosePciIo;
  }

  //
  // PCI IO access granted, configure virtio-blk device.
  //
  Status = VirtioBlkInit (Dev);
  if (EFI_ERROR (Status)) {
    goto RestorePciAttributes;
  }

  //
  // Setup complete, attempt to export the driver instance's BlockIo interface.
  //
  Dev->Signature = VBLK_SIG;
  Status = gBS->InstallProtocolInterface (&DeviceHandle,
                  &gEfiBlockIoProtocolGuid, EFI_NATIVE_INTERFACE,
                  &Dev->BlockIo);
  if (EFI_ERROR (Status)) {
    goto UninitDev;
  }

  return EFI_SUCCESS;

UninitDev:
  VirtioBlkUninit (Dev);

RestorePciAttributes:
  Dev->PciIo->Attributes (Dev->PciIo, EfiPciIoAttributeOperationSet,
                Dev->OriginalPciAttributes, NULL);

ClosePciIo:
  gBS->CloseProtocol (DeviceHandle, &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle, DeviceHandle);

FreeVirtioBlk:
  FreePool (Dev);

  return Status;
}


/**

  Stop driving a virtio-blk device and remove its BlockIo interface.

  This function replays the success path of DriverBindingStart() in reverse.
  The host side virtio-blk device is reset, so that the OS boot loader or the
  OS may reinitialize it.

  @param[in] This               The EFI_DRIVER_BINDING_PROTOCOL object
                                incorporating this driver (independently of any
                                device).

  @param[in] DeviceHandle       Stop driving this device.

  @param[in] NumberOfChildren   Since this function belongs to a device driver
                                only (as opposed to a bus driver), the caller
                                environment sets NumberOfChildren to zero, and
                                we ignore it.

  @param[in] ChildHandleBuffer  Ignored (corresponding to NumberOfChildren).

**/

EFI_STATUS
EFIAPI
VirtioBlkDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL *This,
  IN EFI_HANDLE                  DeviceHandle,
  IN UINTN                       NumberOfChildren,
  IN EFI_HANDLE                  *ChildHandleBuffer
  )
{
  VBLK_DEV   *Dev;
  EFI_STATUS Status;

  Dev = VIRTIO_BLK_FROM_BLOCK_IO (This);

  //
  // If DriverBindingStop() is called with the driver instance still in use,
  // or any of the parameters are invalid, we've caught a bug.
  //
  Status = gBS->UninstallProtocolInterface (DeviceHandle,
                  &gEfiBlockIoProtocolGuid, &Dev->BlockIo);
  ASSERT (Status == EFI_SUCCESS);

  VirtioBlkUninit (Dev);

  Dev->PciIo->Attributes (Dev->PciIo, EfiPciIoAttributeOperationSet,
                Dev->OriginalPciAttributes, NULL);

  gBS->CloseProtocol (DeviceHandle, &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle, DeviceHandle);

  FreePool (Dev);

  return EFI_SUCCESS;
}


//
// The static object that groups the Supported() (ie. probe), Start() and
// Stop() functions of the driver together. Refer to UEFI Spec 2.3.1 + Errata
// C, 10.1 EFI Driver Binding Protocol.
//
STATIC EFI_DRIVER_BINDING_PROTOCOL gDriverBinding = {
  &VirtioBlkDriverBindingSupported,
  &VirtioBlkDriverBindingStart,
  &VirtioBlkDriverBindingStop,
  0x10, // Version, must be in [0x10 .. 0xFFFFFFEF] for IHV-developed drivers
  NULL, // ImageHandle, to be overwritten by
        // EfiLibInstallDriverBindingComponentName2() in VirtioBlkEntryPoint()
  NULL  // DriverBindingHandle, ditto
};


//
// The purpose of the following scaffolding (EFI_COMPONENT_NAME_PROTOCOL and
// EFI_COMPONENT_NAME2_PROTOCOL implementation) is to format the driver's name
// in English, for display on standard console devices. This is recommended for
// UEFI drivers that follow the UEFI Driver Model. Refer to the Driver Writer's
// Guide for UEFI 2.3.1 v1.01, 11 UEFI Driver and Controller Names.
//
// Device type names ("Virtio Block Device") are not formatted because the
// driver supports only that device type. Therefore the driver name suffices
// for unambiguous identification.
//

STATIC GLOBAL_REMOVE_IF_UNREFERENCED
EFI_UNICODE_STRING_TABLE mDriverNameTable[] = {
  { "eng;en", L"Virtio Block Driver" },
  { NULL,     NULL                   }
};

STATIC GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME_PROTOCOL gComponentName;

EFI_STATUS
EFIAPI
VirtioBlkGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL *This,
  IN  CHAR8                       *Language,
  OUT CHAR16                      **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mDriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gComponentName) // Iso639Language
           );
}

EFI_STATUS
EFIAPI
VirtioBlkGetDeviceName (
  IN  EFI_COMPONENT_NAME_PROTOCOL *This,
  IN  EFI_HANDLE                  DeviceHandle,
  IN  EFI_HANDLE                  ChildHandle,
  IN  CHAR8                       *Language,
  OUT CHAR16                      **ControllerName
  )
{
  return EFI_UNSUPPORTED;
}

STATIC GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME_PROTOCOL gComponentName = {
  &VirtioBlkGetDriverName,
  &VirtioBlkGetDeviceName,
  "eng" // SupportedLanguages, ISO 639-2 language codes
};

STATIC GLOBAL_REMOVE_IF_UNREFERENCED
EFI_COMPONENT_NAME2_PROTOCOL gComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME)     &VirtioBlkGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) &VirtioBlkGetDeviceName,
  "en" // SupportedLanguages, RFC 4646 language codes
};


//
// Entry point of this driver.
//
EFI_STATUS
EFIAPI
VirtioBlkEntryPoint (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  return EfiLibInstallDriverBindingComponentName2 (
           ImageHandle,
           SystemTable,
           &gDriverBinding,
           ImageHandle,
           &gComponentName,
           &gComponentName2
           );
}

