#include <Arduino.h>

// Alphabets
const char* limited_alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ"; // 24 chars; no I, O
const char* full_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";  // 26 chars

// --- Struct Definitions ---

struct StrideMapping {
  uint32_t start;
  uint32_t s1;
  uint32_t s2;
  const char* prefix;
  const char* first; // nullable (nullptr)
  const char* last;  // nullable (nullptr)
  bool use_full_alphabet;
  
  // Derived fields (calculated once on first run)
  uint32_t offset;
  uint32_t end;
};

struct NumericMapping {
  uint32_t start;
  uint32_t first;
  uint32_t count;
  const char* template_str;
  
  // Derived field
  uint32_t end;
};

// --- Mappings ---

StrideMapping stride_mappings[] = {
  { 0x380000, 1024, 32, "F-B", nullptr, nullptr, true, 0, 0 },
  { 0x388000, 1024, 32, "F-I", nullptr, nullptr, true, 0, 0 },
  { 0x390000, 1024, 32, "F-G", nullptr, nullptr, true, 0, 0 },
  { 0x398000, 1024, 32, "F-H", nullptr, nullptr, true, 0, 0 },
  { 0x3A0000, 1024, 32, "F-O", nullptr, nullptr, true, 0, 0 },
  { 0x3C4421, 1024, 32, "D-A", "AAA", "OZZ", true, 0, 0 },
  { 0x3C0001, 26*26, 26, "D-A", "PAA", "ZZZ", true, 0, 0 },
  { 0x3C8421, 1024, 32, "D-B", "AAA", "OZZ", true, 0, 0 },
  { 0x3C2001, 26*26, 26, "D-B", "PAA", "ZZZ", true, 0, 0 },
  { 0x3CC000, 26*26, 26, "D-C", nullptr, nullptr, true, 0, 0 },
  { 0x3D04A8, 26*26, 26, "D-E", nullptr, nullptr, true, 0, 0 },
  { 0x3D4950, 26*26, 26, "D-F", nullptr, nullptr, true, 0, 0 },
  { 0x3D8DF8, 26*26, 26, "D-G", nullptr, nullptr, true, 0, 0 },
  { 0x3DD2A0, 26*26, 26, "D-H", nullptr, nullptr, true, 0, 0 },
  { 0x3E1748, 26*26, 26, "D-I", nullptr, nullptr, true, 0, 0 },
  { 0x448421, 1024, 32, "OO-", nullptr, nullptr, true, 0, 0 },
  { 0x458421, 1024, 32, "OY-", nullptr, nullptr, true, 0, 0 },
  { 0x460000, 26*26, 26, "OH-", nullptr, nullptr, true, 0, 0 },
  { 0x468421, 1024, 32, "SX-", nullptr, nullptr, true, 0, 0 },
  { 0x490421, 1024, 32, "CS-", nullptr, nullptr, true, 0, 0 },
  { 0x4A0421, 1024, 32, "YR-", nullptr, nullptr, true, 0, 0 },
  { 0x4B8421, 1024, 32, "TC-", nullptr, nullptr, true, 0, 0 },
  { 0x740421, 1024, 32, "JY-", nullptr, nullptr, true, 0, 0 },
  { 0x760421, 1024, 32, "AP-", nullptr, nullptr, true, 0, 0 },
  { 0x768421, 1024, 32, "9V-", nullptr, nullptr, true, 0, 0 },
  { 0x778421, 1024, 32, "YK-", nullptr, nullptr, true, 0, 0 },
  { 0xC00001, 26*26, 26, "C-F", nullptr, nullptr, true, 0, 0 },
  { 0xC044A9, 26*26, 26, "C-G", nullptr, nullptr, true, 0, 0 },
  { 0xE01041, 4096, 64, "LV-", nullptr, nullptr, true, 0, 0 }
};

NumericMapping numeric_mappings[] = {
  { 0x140000, 0, 100000, "RA-00000", 0 },
  { 0x0B03E8, 1000, 1000, "CU-T0000", 0 }
};

const int NUM_STRIDE_MAPPINGS = sizeof(stride_mappings) / sizeof(StrideMapping);
const int NUM_NUMERIC_MAPPINGS = sizeof(numeric_mappings) / sizeof(NumericMapping);

// --- Initialization Logic ---

bool mappings_initialized = false;

int indexOf(const char* alphabet, char c) {
  const char* ptr = strchr(alphabet, c);
  if (ptr) return ptr - alphabet;
  return -1;
}

void initMappings() {
  if (mappings_initialized) return;

  for (int i = 0; i < NUM_STRIDE_MAPPINGS; ++i) {
    StrideMapping& m = stride_mappings[i];
    const char* alpha = m.use_full_alphabet ? full_alphabet : limited_alphabet;
    int alpha_len = strlen(alpha);

    if (m.first != nullptr) {
      int c1 = indexOf(alpha, m.first[0]);
      int c2 = indexOf(alpha, m.first[1]);
      int c3 = indexOf(alpha, m.first[2]);
      m.offset = c1 * m.s1 + c2 * m.s2 + c3;
    } else {
      m.offset = 0;
    }

    if (m.last != nullptr) {
      int c1 = indexOf(alpha, m.last[0]);
      int c2 = indexOf(alpha, m.last[1]);
      int c3 = indexOf(alpha, m.last[2]);
      m.end = m.start - m.offset + c1 * m.s1 + c2 * m.s2 + c3;
    } else {
      m.end = m.start - m.offset + 
              (alpha_len - 1) * m.s1 + 
              (alpha_len - 1) * m.s2 + 
              (alpha_len - 1);
    }
  }

  for (int i = 0; i < NUM_NUMERIC_MAPPINGS; ++i) {
    numeric_mappings[i].end = numeric_mappings[i].start + numeric_mappings[i].count - 1;
  }

  mappings_initialized = true;
}

// --- Allocation Algorithms ---

String stride_reg(uint32_t hexid) {
  for (int i = 0; i < NUM_STRIDE_MAPPINGS; ++i) {
    StrideMapping& m = stride_mappings[i];
    if (hexid < m.start || hexid > m.end) continue;

    uint32_t offset = hexid - m.start + m.offset;

    int32_t i1 = offset / m.s1;
    offset = offset % m.s1;
    int32_t i2 = offset / m.s2;
    offset = offset % m.s2;
    int32_t i3 = offset;

    const char* alpha = m.use_full_alphabet ? full_alphabet : limited_alphabet;
    int alpha_len = strlen(alpha);

    if (i1 < 0 || i1 >= alpha_len || i2 < 0 || i2 >= alpha_len || i3 < 0 || i3 >= alpha_len) {
      continue;
    }

    String reg = String(m.prefix);
    reg += alpha[i1];
    reg += alpha[i2];
    reg += alpha[i3];
    return reg;
  }
  return "";
}

String numeric_reg(uint32_t hexid) {
  for (int i = 0; i < NUM_NUMERIC_MAPPINGS; ++i) {
    NumericMapping& m = numeric_mappings[i];
    if (hexid < m.start || hexid > m.end) continue;

    String reg = String(hexid - m.start + m.first);
    String tpl = String(m.template_str);
    return tpl.substring(0, tpl.length() - reg.length()) + reg;
  }
  return "";
}

String n_letter(int32_t rem) {
  if (rem == 0) return "";
  --rem;
  return String(limited_alphabet[rem]);
}

String n_letters(int32_t rem) {
  if (rem == 0) return "";
  --rem;
  String res = "";
  res += limited_alphabet[rem / 25];
  res += n_letter(rem % 25);
  return res;
}

String n_reg(uint32_t hexid) {
  int32_t offset = (int32_t)hexid - 0xA00001;
  if (offset < 0 || offset >= 915399) return "";

  int32_t digit1 = (offset / 101711) + 1;
  String reg = "N" + String(digit1);
  offset = offset % 101711;
  if (offset <= 600) return reg + n_letters(offset);

  offset -= 601;
  int32_t digit2 = offset / 10111;
  reg += String(digit2);
  offset = offset % 10111;
  if (offset <= 600) return reg + n_letters(offset);

  offset -= 601;
  int32_t digit3 = offset / 951;
  reg += String(digit3);
  offset = offset % 951;
  if (offset <= 600) return reg + n_letters(offset);

  offset -= 601;
  int32_t digit4 = offset / 35;
  reg += String(digit4);
  offset = offset % 35;
  if (offset <= 24) return reg + n_letter(offset);

  offset -= 25;
  reg += String(offset);
  return reg;
}

String hl_reg(uint32_t hexid) {
  if (hexid >= 0x71BA00 && hexid <= 0x71bf99) {
    String hexStr = String(hexid - 0x71BA00 + 0x7200, HEX);
    hexStr.toUpperCase();
    return "HL" + hexStr;
  }
  if (hexid >= 0x71C000 && hexid <= 0x71C099) {
    String hexStr = String(hexid - 0x71C000 + 0x8000, HEX);
    hexStr.toUpperCase();
    return "HL" + hexStr;
  }
  if (hexid >= 0x71C200 && hexid <= 0x71C299) {
    String hexStr = String(hexid - 0x71C200 + 0x8200, HEX);
    hexStr.toUpperCase();
    return "HL" + hexStr;
  }
  return "";
}

String ja_reg(uint32_t hexid) {
  int32_t offset = (int32_t)hexid - 0x840000;
  if (offset < 0 || offset >= 229840) return "";

  String reg = "JA";

  int32_t digit1 = offset / 22984;
  if (digit1 < 0 || digit1 > 9) return "";
  reg += String(digit1);
  offset = offset % 22984;

  int32_t digit2 = offset / 916;
  if (digit2 < 0 || digit2 > 9) return "";
  reg += String(digit2);
  offset = offset % 916;

  if (offset < 340) {
    int32_t digit3 = offset / 34;
    reg += String(digit3);
    offset = offset % 34;

    if (offset < 10) return reg + String(offset);

    offset -= 10;
    reg += limited_alphabet[offset];
    return reg;
  }

  offset -= 340;
  int32_t letter3 = offset / 24;
  reg += limited_alphabet[letter3];
  reg += limited_alphabet[offset % 24];
  return reg;
}

// --- Main Lookup Interface ---

/**
 * Parses a Hexadecimal String (e.g., "A00001") and attempts 
 * to decode its aircraft registration mapping.
 * Returns an empty String "" if no match is found or on invalid input.
 */
String registration_from_hexid(String hexidStr) {
  // Ensure the derived offset tables are populated (runs once)
  if (!mappings_initialized) {
    initMappings();
  }

  // Convert Hex String safely to uint32_t
  char* endPtr;
  uint32_t hexid = strtoul(hexidStr.c_str(), &endPtr, 16);
  
  // Guard against invalid/empty strings returning 0x0 inappropriately
  if (hexid == 0 && (hexidStr.length() == 0 || endPtr == hexidStr.c_str())) {
    return "";
  }

  String reg;
  
  reg = n_reg(hexid);
  if (reg != "") return reg;

  reg = ja_reg(hexid);
  if (reg != "") return reg;

  reg = hl_reg(hexid);
  if (reg != "") return reg;

  reg = numeric_reg(hexid);
  if (reg != "") return reg;

  reg = stride_reg(hexid);
  if (reg != "") return reg;

  return ""; // Null/No match
}


// --- ARDUINO EXAMPLE USAGE ---

void setup() {
  Serial.begin(115200);
  
  // Test a few known registrations to verify the algorithm
  Serial.println("Starting ICAO lookup tests...");
  
  // US N-Number
  Serial.println("A00001 -> " + registration_from_hexid("A00001")); // Expected N1
  
  // Japan JA-Number
  Serial.println("840000 -> " + registration_from_hexid("840000")); // Expected JA0000
  
  // Germany D-Number
  Serial.println("3C4421 -> " + registration_from_hexid("3C4421")); // Expected D-AAAA
  
  // Canada C-Number
  Serial.println("C00001 -> " + registration_from_hexid("C00001")); // Expected C-FAAA
}

void loop() {
  // Leave empty for testing
}