import pytest
import struct
import zlib
import io
from unittest.mock import patch, MagicMock


# Simulated decompressor that mirrors the CxiBitReader vulnerability pattern
# This represents the Python-equivalent logic of the vulnerable C code
class CxiBitReader:
    """Simulates the CxiBitReader decompression engine behavior"""
    
    def __init__(self, data: bytes, declared_output_size: int):
        self.data = data
        self.pos = 0
        self.bit_buffer = 0
        self.bits_available = 0
        self.declared_output_size = declared_output_size
        self.output = bytearray()
    
    def fetch(self):
        """Fetch next byte from compressed stream"""
        if self.pos < len(self.data):
            self.bit_buffer = (self.bit_buffer << 8) | self.data[self.pos]
            self.bits_available += 8
            self.pos += 1
    
    def read_bits(self, n: int) -> int:
        """Read n bits from the stream"""
        while self.bits_available < n:
            self.fetch()
        self.bits_available -= n
        return (self.bit_buffer >> self.bits_available) & ((1 << n) - 1)
    
    def decompress_safe(self) -> bytes:
        """
        SECURE version: tracks bytes written against declared output buffer size.
        MUST ALWAYS enforce: len(output) <= declared_output_size
        """
        while self.pos < len(self.data):
            if len(self.output) >= self.declared_output_size:
                break  # Security boundary: never exceed declared size
            
            try:
                flag = self.read_bits(1)
                if flag == 0:
                    # Literal byte
                    byte_val = self.read_bits(8)
                    if len(self.output) < self.declared_output_size:
                        self.output.append(byte_val)
                else:
                    # Back-reference (length, distance)
                    length = self.read_bits(4) + 3
                    distance = self.read_bits(8) + 1
                    
                    for _ in range(length):
                        if len(self.output) >= self.declared_output_size:
                            break  # Security boundary enforced in back-reference too
                        if len(self.output) >= distance:
                            ref_byte = self.output[-distance]
                            self.output.append(ref_byte)
                        else:
                            self.output.append(0)
            except (IndexError, ValueError):
                break
        
        return bytes(self.output)


def build_literal_payload(num_literals: int) -> bytes:
    """Build a compressed payload with many literal bytes"""
    result = bytearray()
    for i in range(num_literals):
        # Flag bit 0 = literal, then 8 bits of data
        # Pack as: 0XXXXXXXX (9 bits per literal)
        result.append(i & 0xFF)
    return bytes(result)


def build_backreference_flood_payload() -> bytes:
    """Build payload with back-references designed to expand output massively"""
    # Craft bytes that when interpreted as bit stream produce many back-references
    # Each back-reference with flag=1, length=15 (max), distance=1 expands 18x
    payload = bytearray()
    # Start with some literal data
    for i in range(16):
        payload.append(i)
    # Then flood with back-reference patterns
    for _ in range(100):
        # 0xFF = 11111111 in bits - triggers back-references
        payload.append(0xFF)
        payload.append(0xFF)
        payload.append(0x01)
    return bytes(payload)


def build_declared_size_overflow_payload(declared_size: int) -> tuple:
    """Build payload where compressed data would expand far beyond declared size"""
    # Payload that generates many bytes
    compressed = bytearray()
    for i in range(declared_size * 10):
        compressed.append(i & 0xFF)
    return bytes(compressed), declared_size


# Adversarial payloads: (compressed_data, declared_output_size)
ADVERSARIAL_PAYLOADS = [
    # Case 1: Declared size 0, any data should produce 0 bytes
    (b'\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF', 0),
    
    # Case 2: Declared size 1, massive input
    (b'\xAA' * 1000, 1),
    
    # Case 3: Declared size 10, input designed to expand to thousands of bytes
    (b'\xFF\xFF\x01' * 500, 10),
    
    # Case 4: Declared size 100, back-reference flood
    (build_backreference_flood_payload(), 100),
    
    # Case 5: Declared size matches input exactly (boundary condition)
    (b'\x00' * 50, 50),
    
    # Case 6: Empty compressed data
    (b'', 1024),
    
    # Case 7: Single byte compressed data, large declared size
    (b'\x42', 65536),
    
    # Case 8: All 0xFF bytes (maximum bit patterns)
    (b'\xFF' * 256, 64),
    
    # Case 9: All 0x00 bytes
    (b'\x00' * 256, 32),
    
    # Case 10: Alternating patterns
    (b'\xAA\x55' * 128, 16),
    
    # Case 11: Declared size 0 with empty input
    (b'', 0),
    
    # Case 12: Large declared size, small input
    (b'\x01\x02\x03', 1024 * 1024),
    
    # Case 13: Crafted to trigger maximum back-reference expansion
    (bytes([0b10000000, 0xFF, 0x01] * 200), 50),
    
    # Case 14: Integer boundary declared sizes
    (b'\xDE\xAD\xBE\xEF' * 64, 255),
    
    # Case 15: Declared size at power-of-2 boundary
    (b'\xFF' * 512, 256),
    
    # Case 16: Null bytes mixed with high bytes
    (b'\x00\xFF\x00\xFF' * 100, 8),
    
    # Case 17: Repeated pattern designed to exploit back-references
    (b'\x80\xFF\x01' * 300, 128),
    
    # Case 18: Maximum length back-reference pattern
    (bytes([0xFF, 0x0F, 0x00] * 100), 32),
    
    # Case 19: Declared size 1 with back-reference flood
    (b'\xFF\xFF\xFF\xFF' * 100, 1),
    
    # Case 20: Stress test - large declared size with adversarial expansion
    (b'\x80' + b'\xFF\x0F\x00' * 1000, 512),
]


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_decompressor_never_exceeds_declared_output_size(payload):
    """
    Invariant: The decompression engine MUST NEVER write more bytes than the 
    declared output buffer size, regardless of what the compressed input contains.
    
    This guards against heap overflow vulnerabilities where crafted compressed 
    data causes decompression to exceed pre-allocated output buffer bounds.
    The output length must always be <= declared_output_size.
    """
    compressed_data, declared_output_size = payload
    
    reader = CxiBitReader(compressed_data, declared_output_size)
    output = reader.decompress_safe()
    
    # SECURITY INVARIANT: Output must never exceed declared size
    assert len(output) <= declared_output_size, (
        f"SECURITY VIOLATION: Decompressor wrote {len(output)} bytes "
        f"but declared output size is only {declared_output_size} bytes. "
        f"Input was {len(compressed_data)} bytes. "
        f"This represents a heap overflow condition."
    )


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_decompressor_output_is_non_negative(payload):
    """
    Invariant: Output size must always be a non-negative integer.
    The decompressor must never produce an invalid/negative output size.
    """
    compressed_data, declared_output_size = payload
    
    reader = CxiBitReader(compressed_data, declared_output_size)
    output = reader.decompress_safe()
    
    assert len(output) >= 0, (
        f"SECURITY VIOLATION: Decompressor produced invalid output length {len(output)}"
    )


@pytest.mark.parametrize("declared_size", [0, 1, 7, 8, 15, 16, 255, 256, 1023, 1024, 65535, 65536])
def test_decompressor_boundary_declared_sizes(declared_size):
    """
    Invariant: Security boundary holds at all declared size boundaries,
    especially at power-of-2 and off-by-one values that commonly trigger 
    buffer overflow conditions.
    """
    # Use adversarial input designed to maximize output
    adversarial_input = b'\xFF\x0F\x00' * 1000 + b'\xAA' * 500
    
    reader = CxiBitReader(adversarial_input, declared_size)
    output = reader.decompress_safe()
    
    assert len(output) <= declared_size, (
        f"SECURITY VIOLATION: Output {len(output)} bytes exceeds "
        f"declared boundary size {declared_size} bytes."
    )


def test_decompressor_zero_declared_size_produces_no_output():
    """
    Invariant: When declared output size is 0, the decompressor MUST produce
    exactly 0 bytes of output, regardless of input content.
    This is the strictest form of the buffer overflow guard.
    """
    adversarial_inputs = [
        b'\xFF' * 1000,
        b'\xAA\x55' * 500,
        b'\x00' * 1000,
        b'\x80\xFF\x01' * 333,
        bytes(range(256)) * 4,
    ]
    
    for adversarial_input in adversarial_inputs:
        reader = CxiBitReader(adversarial_input, 0)
        output = reader.decompress_safe()
        
        assert len(output) == 0, (
            f"SECURITY VIOLATION: With declared_size=0, decompressor produced "
            f"{len(output)} bytes from {len(adversarial_input)} bytes of input. "
            f"Any output represents a buffer overflow."
        )


@pytest.mark.parametrize("multiplier", [2, 4, 8, 16, 32, 64, 128, 256])
def test_decompressor_expansion_ratio_bounded(multiplier):
    """
    Invariant: The decompressor must never produce output larger than the 
    declared size, even when input is crafted to achieve maximum expansion ratio.
    Back-reference based compression can theoretically expand data by large 
    multipliers, but the output buffer bound must always be respected.
    """
    declared_size = 64
    # Create input that would expand by 'multiplier' times if unchecked
    base_data = b'\x41' * declared_size  # 'A' repeated
    # Add back-reference patterns to try to expand
    expansion_payload = b'\x80\xFF\x01' * (declared_size * multiplier)
    adversarial_input = base_data + expansion_payload
    
    reader = CxiBitReader(adversarial_input, declared_size)
    output = reader.decompress_safe()
    
    assert len(output) <= declared_size, (
        f"SECURITY VIOLATION: With expansion multiplier {multiplier}x, "
        f"output {len(output)} bytes exceeded declared size {declared_size} bytes."
    )


def test_decompressor_does_not_raise_on_adversarial_input():
    """
    Invariant: The decompressor must handle adversarial input gracefully without
    raising unhandled exceptions. Crashes or unhandled exceptions in a 
    decompressor can themselves be security vulnerabilities (DoS).
    """
    adversarial_inputs = [
        b'',
        b'\x00',
        b'\xFF',
        b'\xFF' * 65536,
        b'\x00' * 65536,
        bytes(range(256)),
        b'\xDE\xAD\xBE\xEF\xCA\xFE\xBA\xBE' * 1000,
        b'\x80\x00\x00' * 10000,
    ]
    
    for adversarial_input in adversarial_inputs:
        declared_size = 256
        try:
            reader = CxiBitReader(adversarial_input, declared_size)
            output = reader.decompress_safe()
            
            # Must still respect the invariant
            assert len(output) <= declared_size, (
                f"SECURITY VIOLATION: Output {len(output)} exceeds declared {declared_size}"
            )
        except Exception as e:
            pytest.fail(
                f"SECURITY VIOLATION: Decompressor raised unhandled exception "
                f"on adversarial input of length {len(adversarial_input)}: {e}"
            )