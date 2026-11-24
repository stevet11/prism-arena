// Tiny stroke font. Each glyph is a set of polylines on a 5x7 grid (x 0-4, y 0-6, y down).
// Strokes are separated by spaces; each stroke is a run of "xy" digit pairs.
#pragma once

inline const char* glyph(char c) {
    switch (c) {
        case 'A': return "0602204246 0343";
        case 'B': return "0006 003041423303 3344453606";
        case 'C': return "4130100105163645";
        case 'D': return "00304145360600";
        case 'E': return "40000646 0333";
        case 'F': return "400006 0333";
        case 'G': return "41301001051636454323";
        case 'H': return "0006 4046 0343";
        case 'I': return "1030 2026 1636";
        case 'J': return "4045361605";
        case 'K': return "0006 4003 0346";
        case 'L': return "000646";
        case 'M': return "0600234046";
        case 'N': return "06004640";
        case 'O': return "103041453616050110";
        case 'P': return "06003041423303";
        case 'Q': return "103041453616050110 2446";
        case 'R': return "06003041423303 2346";
        case 'S': return "413010010213334445361605";
        case 'T': return "0040 2026";
        case 'U': return "000516364540";
        case 'V': return "002640";
        case 'W': return "0016233640";
        case 'X': return "0046 4006";
        case 'Y': return "0023 4023 2326";
        case 'Z': return "00400646";
        case '0': return "103041453616050110 4105";
        case '1': return "112026 1636";
        case '2': return "01103041420646";
        case '3': return "0110304142334445361605 1333";
        case '4': return "36300444";
        case '5': return "400002324345361605";
        case '6': return "30100105163645443303";
        case '7': return "004016";
        case '8': return "103041423313020110 1304051636454433";
        case '9': return "43130201103041453616";
        case ':': return "2122 2425";
        case '.': return "2526";
        case ',': return "252617";
        case '-': return "1333";
        case '+': return "1333 2224";
        case '!': return "2024 2526";
        case '/': return "0640";
        case '?': return "011030414223 2526";
        case '(': return "30121436";
        case ')': return "10323416";
        default: return "";
    }
}
