#define _USE_MATH_DEFINES

#include "framework.h"
#include "VectorField.h"
#include <commdlg.h>

#include <sstream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cwctype>
#include <iomanip>

using namespace std;

// ============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================================
int g_ActiveView = 0;
int g_VariantNum = 0;
bool g_IsCalculated = false;
wstring g_FieldAStr = L"";
wstring g_FieldBStr = L"";
wstring g_StatusMessage = L"Добро пожаловать! Импортируйте TXT-файл с заданием для начала работы.";
HBITMAP g_hPlotBitmapA = NULL;
HBITMAP g_hPlotBitmapB = NULL;

// ============================================================================
// ПРОТОТИПЫ ФУНКЦИЙ ИНТЕРФЕЙСА И ЛОГИКИ
// ============================================================================
enum ErrCode { ErrParse, WarnNoData, WarnNoCalc, ErrWrite, ErrWlScript, ErrWlNotFound, ErrWlImage };
void ShowError(HWND hWnd, ErrCode code);

bool GetLoadFileNameExplorer(HWND hWnd, wstring& outPath);
bool GetSaveFileNameExplorer(HWND hWnd, wstring& outPath);
bool ImportDataFromFile(const wstring& filename);
bool ExportDataToFile(const wstring& filename);
wstring BuildInputDataText();
wstring BuildReportText();
void CreateInterfaceButtons(HWND hWnd, HINSTANCE hInstance);
void DrawBackground(HDC hdc, RECT rect);
void DrawStatusAndCard(HDC hdc, RECT rect);
void HandlePaint(HWND hWnd);
void HandleCmdImportShow(HWND hWnd, int wmId);
void HandleCmdCalcExport(HWND hWnd, int wmId);
void HandleCmdVis(HWND hWnd, int wmId);
void HandleWmCommand(HWND hWnd, WPARAM wParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// Функции перевода и генерации графиков в Wolfram
wstring PolyToWolframString(const Polynomial& p);
bool WriteScript(bool isA, const wstring& px, const wstring& py, const wstring& pz, const wstring& scr, const wstring& bmp);
bool RunWolfram(const wstring& scr);
bool GenerateWolframPlot(bool isFieldA, ErrCode& outErr);

// Функции геометрического расчета и рендеринга интерфейса (GDI)
void GetDrawCoords(const BITMAP& bmp, const RECT& r, int lblH, int& dW, int& dH, int& dX, int& dY);
void DrawLabels(HDC hdc, int dX, int dW, int dY, int dH, int lblH);
void DrawVisualization(HDC hdc, RECT rect);

// Инициализация статических членов
Polynomial FieldMathCalculations::fieldA_X; Polynomial FieldMathCalculations::fieldA_Y; Polynomial FieldMathCalculations::fieldA_Z;
Polynomial FieldMathCalculations::fieldB_X; Polynomial FieldMathCalculations::fieldB_Y; Polynomial FieldMathCalculations::fieldB_Z;

Polynomial::Polynomial(initializer_list<Monomial> init) : terms(init) {}
Polynomial::Polynomial() {}
Polynomial FieldMathCalculations::GetFieldBX() { return fieldB_X; }
Polynomial FieldMathCalculations::GetFieldBY() { return fieldB_Y; }
Polynomial FieldMathCalculations::GetFieldBZ() { return fieldB_Z; }

// ============================================================================
// РЕАЛИЗАЦИЯ МОДУЛЕЙ
// ============================================================================

// 1. Модуль SetVariableToZero
Polynomial Polynomial::SetVariableToZero(bool zeroX, bool zeroY, bool zeroZ) const {
    Polynomial result;
    for (const auto& term : terms) {
        if ((zeroX && term.px > 0) || (zeroY && term.py > 0) || (zeroZ && term.pz > 0)) continue;
        result.terms.push_back(term);
    }
    return result;
}

// 2. Модуль IntegrateSymbolically
Polynomial Polynomial::IntegrateSymbolically(int varIndex) const {
    Polynomial result;
    for (const auto& term : terms) {
        Monomial m = term;
        if (varIndex == 0) { m.px += 1; m.coef /= (double)m.px; }
        else if (varIndex == 1) { m.py += 1; m.coef /= (double)m.py; }
        else if (varIndex == 2) { m.pz += 1; m.coef /= (double)m.pz; }
        result.terms.push_back(m);
    }
    return result;
}

// 3. Модуль Add
void Polynomial::Add(const Polynomial& other) {
    for (const auto& otherTerm : other.terms) {
        bool found = false;
        for (auto& myTerm : terms) {
            if (myTerm.px == otherTerm.px && myTerm.py == otherTerm.py && myTerm.pz == otherTerm.pz) {
                myTerm.coef += otherTerm.coef; found = true; break;
            }
        }
        if (!found) terms.push_back(otherTerm);
    }
    terms.erase(remove_if(terms.begin(), terms.end(), [](const Monomial& m) { return abs(m.coef) < 1e-6; }), terms.end());
}

// 4. Модуль Evaluate
double Polynomial::Evaluate(double x, double y, double z) const {
    double sum = 0.0;
    for (const auto& t : terms) sum += t.coef * pow(x, t.px) * pow(y, t.py) * pow(z, t.pz);
    return sum;
}

// 5. Модуль ToWString
wstring Polynomial::ToWString() const {
    if (terms.empty()) return L"0";
    wstringstream ss; bool first = true;
    for (const auto& term : terms) {
        if (term.coef > 0 && !first) ss << L" + ";
        if (term.coef < 0) ss << (first ? L"-" : L" - ");
        double absCoef = abs(term.coef);
        if (absCoef != 1.0 || (term.px == 0 && term.py == 0 && term.pz == 0)) ss << setprecision(4) << absCoef;
        if (term.px > 0) { ss << L"x"; if (term.px > 1) ss << L"^" << term.px; }
        if (term.py > 0) { ss << L"y"; if (term.py > 1) ss << L"^" << term.py; }
        if (term.pz > 0) { ss << L"z"; if (term.pz > 1) ss << L"^" << term.pz; }
        first = false;
    }
    return ss.str();
}

// 6. Модуль ReplaceAll
void FieldMathCalculations::ReplaceAll(wstring& str, const wstring& from, const wstring& to) {
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != wstring::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

// 7. Модуль ParsePoly
Polynomial FieldMathCalculations::ParsePoly(wstring expr) {
    Polynomial poly; wstring clean;
    for (wchar_t c : expr) {
        if (c == L' ' || c == L'(' || c == L')' || c == L'*') continue;
        if (c == L'-') { clean += L"+-"; continue; } clean += c;
    }
    wstringstream ss(clean); wstring token;
    while (getline(ss, token, L'+')) {
        if (token.empty()) continue;
        Monomial m = { 1.0, 0, 0, 0 }; size_t i = (token[0] == L'-') ? 1 : 0;
        wstring coef = (token[0] == L'-') ? L"-" : L"";
        while (i < token.length() && (iswdigit(token[i]) || token[i] == L'.')) coef += token[i++];
        m.coef = (coef == L"-" || coef.empty()) ? ((coef == L"-") ? -1.0 : 1.0) : stod(coef);
        auto pV = [&](wchar_t v, int& p) { size_t pos = token.find(v); if (pos != wstring::npos) { p = 1; if (pos + 1 < token.length() && token[pos + 1] == L'^') p = stoi(token.substr(pos + 2)); } };
        pV(L'x', m.px); pV(L'y', m.py); pV(L'z', m.pz);
        poly.terms.push_back(m);
    }
    return poly;
}

// 8. Модуль ExtractComponents
void FieldMathCalculations::ExtractComponents(wstring line, Polynomial& P, Polynomial& Q, Polynomial& R) {
    ReplaceAll(line, L"\\overline{i}", L"i"); ReplaceAll(line, L"\\overline{j}", L"j");
    ReplaceAll(line, L"\\overline{k}", L"k"); ReplaceAll(line, L"\\overline{a}=", L""); ReplaceAll(line, L"\\overline{b}=", L"");
    size_t i_p = line.find(L"i"), j_p = line.find(L"j"), k_p = line.find(L"k");
    if (i_p != wstring::npos && j_p != wstring::npos && k_p != wstring::npos) {
        auto trimP = [](wstring s) { size_t i = 0; while (i < s.length() && (s[i] == L' ' || s[i] == L'+')) i++; return s.substr(i); };
        P = ParsePoly(line.substr(0, i_p)); Q = ParsePoly(trimP(line.substr(i_p + 1, j_p - i_p - 1))); R = ParsePoly(trimP(line.substr(j_p + 1, k_p - j_p - 1)));
    }
    else {
        wstringstream ss(line); wstring item;
        if (getline(ss, item, L';')) P = ParsePoly(item);
        if (getline(ss, item, L';')) Q = ParsePoly(item);
        if (getline(ss, item, L';')) R = ParsePoly(item);
    }
}

// 9. Модуль ParsePolynomials
void FieldMathCalculations::ParsePolynomials(const wstring& a_str, const wstring& b_str) {
    ExtractComponents(a_str, fieldA_X, fieldA_Y, fieldA_Z);
    ExtractComponents(b_str, fieldB_X, fieldB_Y, fieldB_Z);
}

// 10. Модуль EvaluateFieldA
Vector3D FieldMathCalculations::EvaluateFieldA(double x, double y, double z) {
    return { fieldA_X.Evaluate(x, y, z), fieldA_Y.Evaluate(x, y, z), fieldA_Z.Evaluate(x, y, z) };
}

// 11. Модуль EvaluateFieldB
Vector3D FieldMathCalculations::EvaluateFieldB(double x, double y, double z) {
    return { GetFieldBX().Evaluate(x, y, z), GetFieldBY().Evaluate(x, y, z), GetFieldBZ().Evaluate(x, y, z) };
}

// 12. Модуль Calc_PartialDerivative
double FieldMathCalculations::Calc_PartialDerivative(FUNC3D f, int variable, double x, double y, double z) {
    double h = 1e-4;
    if (variable == 1) return (f(x + h, y, z) - f(x - h, y, z)) / (2 * h);
    if (variable == 2) return (f(x, y + h, z) - f(x, y - h, z)) / (2 * h);
    return (f(x, y, z + h) - f(x, y, z - h)) / (2 * h);
}

// 13. Модуль IntegrateQuarterDisk
double FieldMathCalculations::IntegrateQuarterDisk(function<double(double, double)> func, double radius, int steps) {
    double dr = radius / steps, dphi = (M_PI / 2.0) / steps, sum = 0.0;
    for (int i = 0; i < steps; i++) {
        double r = (i + 0.5) * dr;
        for (int j = 0; j < steps; j++) {
            sum += func(r * cos((j + 0.5) * dphi), r * sin((j + 0.5) * dphi)) * r;
        }
    }
    return sum * dr * dphi;
}

// 14. Модуль Calc_Divergence
double FieldMathCalculations::Calc_Divergence(FUNC3D_VEC field, double x, double y, double z) {
    FUNC3D P = [&](double px, double py, double pz) { return field(px, py, pz).x; };
    FUNC3D Q = [&](double px, double py, double pz) { return field(px, py, pz).y; };
    FUNC3D R = [&](double px, double py, double pz) { return field(px, py, pz).z; };
    return Calc_PartialDerivative(P, 1, x, y, z) + Calc_PartialDerivative(Q, 2, x, y, z) + Calc_PartialDerivative(R, 3, x, y, z);
}

// 15. Модуль Calc_Rotor
Vector3D FieldMathCalculations::Calc_Rotor(FUNC3D_VEC field, double x, double y, double z) {
    FUNC3D P = [&](double px, double py, double pz) { return field(px, py, pz).x; };
    FUNC3D Q = [&](double px, double py, double pz) { return field(px, py, pz).y; };
    FUNC3D R = [&](double px, double py, double pz) { return field(px, py, pz).z; };
    return {
        Calc_PartialDerivative(R, 2, x, y, z) - Calc_PartialDerivative(Q, 3, x, y, z),
        Calc_PartialDerivative(P, 3, x, y, z) - Calc_PartialDerivative(R, 1, x, y, z),
        Calc_PartialDerivative(Q, 1, x, y, z) - Calc_PartialDerivative(P, 2, x, y, z)
    };
}

// 16. Модуль DetermineFieldClass
wstring FieldMathCalculations::DetermineFieldClass(FUNC3D_VEC field) {
    bool is_div = true, is_rot = true;
    for (double x = 0.2; x <= 0.8; x += 0.3) {
        for (double y = 0.2; y <= 0.8; y += 0.3) {
            if (abs(Calc_Divergence(field, x, y, 0.5)) > 1e-3) is_div = false;
            Vector3D r = Calc_Rotor(field, x, y, 0.5);
            if (abs(r.x) > 1e-3 || abs(r.y) > 1e-3 || abs(r.z) > 1e-3) is_rot = false;
        }
    }
    if (is_div && is_rot) return L"Гармоническое";
    return (is_rot ? L"Потенциальное" : (is_div ? L"Соленоидальное" : L"Общего вида"));
}

// 17. Модуль Calc_Flux_GaussOstrogradsky
double FieldMathCalculations::Calc_Flux_GaussOstrogradsky() {
    double Pc = 0.0; int st = 25;
    double d_r = 1.0 / st, d_p = (M_PI / 2.0) / st, d_t = (M_PI / 2.0) / st;
    for (int i = 0; i < st; ++i) {
        for (int j = 0; j < st; ++j) {
            for (int k = 0; k < st; ++k) {
                double r = (i + 0.5) * d_r, p = (j + 0.5) * d_p, t = (k + 0.5) * d_t;
                double x = r * cos(t) * cos(p), y = r * cos(t) * sin(p), z = r * sin(t);
                Pc += Calc_Divergence(EvaluateFieldA, x, y, z) * (r * r * cos(t)) * d_r * d_p * d_t;
            }
        }
    }
    double P_z0 = -IntegrateQuarterDisk([](double x, double y) { return EvaluateFieldA(x, y, 0.0).z; });
    double P_x0 = -IntegrateQuarterDisk([](double y, double z) { return EvaluateFieldA(0.0, y, z).x; });
    double P_y0 = -IntegrateQuarterDisk([](double x, double z) { return EvaluateFieldA(x, 0.0, z).y; });
    return Pc - (P_z0 + P_x0 + P_y0);
}

// 18. Модуль Calc_Flux_Direct
double FieldMathCalculations::Calc_Flux_Direct() {
    return IntegrateQuarterDisk([](double x, double y) {
        double z = sqrt(fmax(0.0, 1.0 - x * x - y * y));
        if (z < 1e-5) return 0.0;
        Vector3D a = EvaluateFieldA(x, y, z);
        return (a.x * x + a.y * y + a.z * z) / z;
        });
}

// 19. Модуль Calc_Circulation_Stokes
double FieldMathCalculations::Calc_Circulation_Stokes() {
    return IntegrateQuarterDisk([](double x, double y) {
        double z = sqrt(fmax(0.0, 1.0 - x * x - y * y));
        if (z < 1e-5) return 0.0;
        Vector3D rot = Calc_Rotor(EvaluateFieldA, x, y, z);
        return (rot.x * x + rot.y * y + rot.z * z) / z;
        });
}

// 20. Модуль Calc_Circulation_Direct
double FieldMathCalculations::Calc_Circulation_Direct() {
    int steps = 100; double dphi = (M_PI / 2.0) / steps, sum = 0.0;
    for (int i = 0; i < steps; i++) {
        double phi = (i + 0.5) * dphi;
        Vector3D a1 = EvaluateFieldA(cos(phi), sin(phi), 0.0);
        sum += (a1.x * (-sin(phi)) + a1.y * cos(phi));
        Vector3D a2 = EvaluateFieldA(0.0, cos(phi), sin(phi));
        sum += (a2.y * (-sin(phi)) + a2.z * cos(phi));
        Vector3D a3 = EvaluateFieldA(sin(phi), 0.0, cos(phi));
        sum += (a3.z * (-sin(phi)) + a3.x * cos(phi));
    }
    return sum * dphi;
}

// 21. Модуль GetAnalyticalPotential
Polynomial FieldMathCalculations::GetAnalyticalPotential() {
    Polynomial U1 = GetFieldBX().SetVariableToZero(false, true, true).IntegrateSymbolically(0);
    Polynomial U2 = GetFieldBY().SetVariableToZero(false, false, true).IntegrateSymbolically(1);
    Polynomial U3 = GetFieldBZ().SetVariableToZero(false, false, false).IntegrateSymbolically(2);
    Polynomial U; U.Add(U1); U.Add(U2); U.Add(U3);
    return U;
}

// 22. Модуль VerifyPotentialCorrectness
bool FieldMathCalculations::VerifyPotentialCorrectness() {
    Polynomial U = GetAnalyticalPotential();
    FUNC3D U_eval = [&](double px, double py, double pz) { return U.Evaluate(px, py, pz); };
    for (double x = 0.2; x <= 0.8; x += 0.3) {
        for (double y = 0.2; y <= 0.8; y += 0.3) {
            double du_dx = Calc_PartialDerivative(U_eval, 1, x, y, 0.5);
            double du_dy = Calc_PartialDerivative(U_eval, 2, x, y, 0.5);
            if (abs(du_dx - EvaluateFieldB(x, y, 0.5).x) > 0.05 || abs(du_dy - EvaluateFieldB(x, y, 0.5).y) > 0.05) return false;
        }
    }
    return true;
}

// 23. Модуль CheckRotorBZero
bool FieldMathCalculations::CheckRotorBZero() {
    for (double x = 0.2; x <= 0.8; x += 0.3) {
        for (double y = 0.2; y <= 0.8; y += 0.3) {
            Vector3D r = Calc_Rotor(EvaluateFieldB, x, y, 0.5);
            if (abs(r.x) > 1e-3 || abs(r.y) > 1e-3 || abs(r.z) > 1e-3) return false;
        }
    }
    return true;
}

// 24. Модуль GetLoadFileNameExplorer
bool GetLoadFileNameExplorer(HWND hWnd, wstring& outPath) {
    wchar_t filename[MAX_PATH] = L""; OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Текстовые файлы (*.txt)\0*.txt\0Все файлы (*.*)\0*.*\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) { outPath = filename; return true; }
    return false;
}

// 25. Модуль GetSaveFileNameExplorer
bool GetSaveFileNameExplorer(HWND hWnd, wstring& outPath) {
    wchar_t filename[MAX_PATH] = L"output.txt"; OPENFILENAMEW ofn; ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hWnd;
    ofn.lpstrFilter = L"Текстовые файлы (*.txt)\0*.txt\0";
    ofn.lpstrFile = filename; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) { outPath = filename; return true; }
    return false;
}

// 26. Модуль ImportDataFromFile
bool ImportDataFromFile(const wstring& filename) {
    ifstream file(filename);
    if (!file.is_open()) return false;
    string line;
    if (getline(file, line)) { try { g_VariantNum = stoi(line); } catch (...) { return false; } }
    if (getline(file, line)) g_FieldAStr = wstring(line.begin(), line.end());
    if (getline(file, line)) g_FieldBStr = wstring(line.begin(), line.end());
    file.close();
    if (g_VariantNum <= 0 || g_FieldAStr.empty() || g_FieldBStr.empty()) return false;
    FieldMathCalculations::ParsePolynomials(g_FieldAStr, g_FieldBStr);
    return true;
}

// 27. Модуль ExportDataToFile
bool ExportDataToFile(const wstring& filename) {
    wstring report = BuildReportText();
    if (report.empty() || g_VariantNum == 0) return false;
    int s_n = WideCharToMultiByte(CP_UTF8, 0, &report[0], (int)report.size(), NULL, 0, NULL, NULL);
    string utf8_str(s_n, 0);
    WideCharToMultiByte(CP_UTF8, 0, &report[0], (int)report.size(), &utf8_str[0], s_n, NULL, NULL);
    ofstream file(filename, ios::binary);
    if (!file.is_open()) return false;
    const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    file.write((const char*)bom, sizeof(bom)); file.write(utf8_str.c_str(), utf8_str.size()); file.close();
    return true;
}

// 28. Модуль BuildInputDataText
wstring BuildInputDataText() {
    wstringstream ss;
    ss << L"====================================================================\n        ИСХОДНЫЕ ДАННЫЕ ВАРИАНТА\n====================================================================\n\n";
    ss << L"  ► Загружен вариант №:  " << g_VariantNum << L"\n\n  ► Векторное поле ā (исследуется на поток и циркуляцию):\n    ā = " << g_FieldAStr << L"\n\n";
    ss << L"  ► Векторное поле b̄ (исследуется на потенциальность):\n    b̄ = " << g_FieldBStr << L"\n\n";
    ss << L"--------------------------------------------------------------------\n  Всё готово! Нажмите кнопку «Расчёт данных» для получения отчета.";
    return ss.str();
}

// 29. Модуль BuildReportText
wstring BuildReportText() {
    wstringstream rep; rep << fixed << setprecision(4); Polynomial U = FieldMathCalculations::GetAnalyticalPotential();
    rep << L"========== РАССЧИТАННЫЕ ДАННЫЕ (ВАР. №" << g_VariantNum << L") ==========\n\n"
        << L"► 1. КЛАССИФИКАЦИЯ ПОЛЯ ā:\n   • Тип поля: " << FieldMathCalculations::DetermineFieldClass(FieldMathCalculations::EvaluateFieldA) << L"\n\n"
        << L"► 2. ВЫЧИСЛЕНИЕ ПОТОКА ПОВЕРХНОСТИ S (ПОЛЕ ā):\n"
        << L"   • 2.1 Метод Остроградского-Гаусса: P = " << FieldMathCalculations::Calc_Flux_GaussOstrogradsky() << L"\n"
        << L"   • 2.2 Метод прямого интегрирования: P = " << FieldMathCalculations::Calc_Flux_Direct() << L"\n\n"
        << L"► 3. ВЫЧИСЛЕНИЕ ЦИРКУЛЯЦИИ ПО КОНТУРУ S (ПОЛЕ ā):\n"
        << L"   • 3.1 Метод теоремы Стокса: C = " << FieldMathCalculations::Calc_Circulation_Stokes() << L"\n"
        << L"   • 3.2 Метод криволинейного интеграл: C = " << FieldMathCalculations::Calc_Circulation_Direct() << L"\n\n"
        << L"► 4. ИССЛЕДОВАНИЕ ПОЛЯ b̄:\n"
        << L"   • 4.1 Проверка потенциальности: " << (FieldMathCalculations::CheckRotorBZero() ? L"поле потенциально" : L"поле не потенциально") << L"\n"
        << L"   • 4.2 Потенциал U(x, y, z) = " << U.ToWString() << L"\n         U(1, 1, 1) = " << U.Evaluate(1, 1, 1) << L"\n"
        << L"   • 4.3 Проверка градиента: " << (FieldMathCalculations::VerifyPotentialCorrectness() ? L"успешна" : L"не успешна") << L"\n"
        << L"   • 4.4 Работа сил поля W = " << U.Evaluate(1, 1, 1) - U.Evaluate(0, 0, 0) << L"\n\n=========================================================";
    return rep.str();
}

// 30. Модуль CreateInterfaceButtons
void CreateInterfaceButtons(HWND hWnd, HINSTANCE hInstance) {
    int y1 = 45, w = 185, h = 35, step = 190;
    CreateWindowW(L"BUTTON", L"1. Импорт данных", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 0 * step, y1, w, h, hWnd, (HMENU)ID_BTN_IMPORT, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"2. Показать данные", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 1 * step, y1, w, h, hWnd, (HMENU)ID_BTN_SHOW, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"3. Расчёт данных", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 2 * step, y1, w, h, hWnd, (HMENU)ID_BTN_CALC, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"4. Визуализация Поля А", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 3 * step, y1, w, h, hWnd, (HMENU)ID_BTN_VIS_A, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"5. Визуализация Поля Б", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 4 * step, y1, w, h, hWnd, (HMENU)ID_BTN_VIS_B, hInstance, NULL);
    CreateWindowW(L"BUTTON", L"6. Экспорт в TXT", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 5 * step, y1, w, h, hWnd, (HMENU)ID_BTN_EXPORT, hInstance, NULL);
}

// 31. Модуль DrawBackground
void DrawBackground(HDC hdc, RECT rect) {
    HBRUSH hBgBrush = CreateSolidBrush(RGB(240, 244, 248)); FillRect(hdc, &rect, hBgBrush); DeleteObject(hBgBrush);
    HFONT hFontDev = CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOld = (HFONT)SelectObject(hdc, hFontDev); SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(0, 0, 0));
    wstring devInfo = L"Разработчики: Варфоломеев, Великоцкая, Нагороднюк   |   Группа: М3О-225БВ-24";
    RECT devRect = { 25, 10, rect.right - 20, 35 }; DrawTextW(hdc, devInfo.c_str(), -1, &devRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOld); DeleteObject(hFontDev);
}

// 32. Модуль DrawStatusAndCard
void DrawStatusAndCard(HDC hdc, RECT rect) {
    RECT sRect = { 20, 100, rect.right - 20, 130 }; HBRUSH hStatB = CreateSolidBrush(RGB(225, 235, 245)); FillRect(hdc, &sRect, hStatB); DeleteObject(hStatB);
    HFONT hFontS = CreateFontW(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOld = (HFONT)SelectObject(hdc, hFontS); SetTextColor(hdc, RGB(0, 0, 0));
    RECT sText = { 35, 105, rect.right - 35, 125 }; DrawTextW(hdc, g_StatusMessage.c_str(), -1, &sText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT cR = { 20, 145, rect.right - 20, rect.bottom - 20 }; RECT shR = { cR.left + 3, cR.top + 3, cR.right + 3, cR.bottom + 3 };
    HBRUSH hShB = CreateSolidBrush(RGB(220, 225, 230)); FillRect(hdc, &shR, hShB); DeleteObject(hShB);
    HBRUSH hCardB = CreateSolidBrush(RGB(255, 255, 255)); HPEN hCardP = CreatePen(PS_SOLID, 1, RGB(200, 205, 215));
    HPEN hOldP = (HPEN)SelectObject(hdc, hCardP); HBRUSH hOldB = (HBRUSH)SelectObject(hdc, hCardB);
    Rectangle(hdc, cR.left, cR.top, cR.right, cR.bottom);
    SelectObject(hdc, hOldB); SelectObject(hdc, hOldP); DeleteObject(hCardB); DeleteObject(hCardP); SelectObject(hdc, hOld); DeleteObject(hFontS);
}

// 33. Модуль HandlePaint
void HandlePaint(HWND hWnd) {
    PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps); RECT r; GetClientRect(hWnd, &r);
    DrawBackground(hdc, r); DrawStatusAndCard(hdc, r);
    HFONT hF = CreateFontW(20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOld = (HFONT)SelectObject(hdc, hF); SetTextColor(hdc, RGB(0, 0, 0)); RECT tR = { 40, 165, r.right - 40, r.bottom - 40 };
    if (g_ActiveView == 1) { wstring t = BuildInputDataText(); DrawTextW(hdc, t.c_str(), -1, &tR, DT_LEFT | DT_WORDBREAK); }
    else if (g_ActiveView == 2) { wstring t = BuildReportText(); DrawTextW(hdc, t.c_str(), -1, &tR, DT_LEFT | DT_WORDBREAK); }
    else if (g_ActiveView == 3 || g_ActiveView == 4) DrawVisualization(hdc, tR);
    else if (g_ActiveView == 0) {
        wstring t = L"\n\n       ПРОГРАММА РАСЧЕТА ПАРАМЕТРОВ ВЕКТОРНЫХ ПОЛЕЙ\n       --------------------------------------------\n\n"
            L"       Для работы с программой:\n\n       1. Нажмите кнопку «1. Импорт данных» сверху\n       2. Выберите .txt файл с вариантом задания и исходными данными\n"
            L"       3. Нажмите «3. Расчёт данных» для получения сводки\n       4. Используйте кнопки визуализации для отображения полей.\n       5. Сохраните результат через кнопку «6. Экспорт в TXT»\n";
        DrawTextW(hdc, t.c_str(), -1, &tR, DT_LEFT | DT_WORDBREAK);
    }
    SelectObject(hdc, hOld); DeleteObject(hF); EndPaint(hWnd, &ps);
}

// 34. Модуль HandleCmdImportShow
void HandleCmdImportShow(HWND hWnd, int wmId) {
    if (wmId == ID_BTN_IMPORT) {
        wstring f;
        if (GetLoadFileNameExplorer(hWnd, f)) {
            if (ImportDataFromFile(f)) {
                g_StatusMessage = L"✔ Успех! Вариант №" + to_wstring(g_VariantNum);
                g_ActiveView = 1; g_IsCalculated = false;
                if (g_hPlotBitmapA) { DeleteObject(g_hPlotBitmapA); g_hPlotBitmapA = NULL; }
                if (g_hPlotBitmapB) { DeleteObject(g_hPlotBitmapB); g_hPlotBitmapB = NULL; }
            }
            else ShowError(hWnd, ErrParse);
        }
    }
    else if (wmId == ID_BTN_SHOW) {
        if (g_VariantNum == 0) ShowError(hWnd, WarnNoData);
        else { g_StatusMessage = L"► Исходные данные ВАР №" + to_wstring(g_VariantNum); g_ActiveView = 1; }
    }
}

// 35. Модуль HandleCmdCalcExport
void HandleCmdCalcExport(HWND hWnd, int wmId) {
    if (wmId == ID_BTN_CALC) {
        if (g_VariantNum == 0) ShowError(hWnd, WarnNoData);
        else { g_StatusMessage = L"✔ Расчёт выполнен!"; g_ActiveView = 2; g_IsCalculated = true; }
    }
    else if (wmId == ID_BTN_EXPORT) {
        if (!g_IsCalculated) {
            ShowError(hWnd, WarnNoCalc); return;
        }
        wstring s;
        if (GetSaveFileNameExplorer(hWnd, s)) {
            if (ExportDataToFile(s)) g_StatusMessage = L"✔ Отчёт сохранён!";
            else ShowError(hWnd, ErrWrite);
        }
    }
}

// 36. Модуль HandleCmdVis
void HandleCmdVis(HWND hWnd, int wmId) {
    if (wmId == ID_BTN_VIS_A || wmId == ID_BTN_VIS_B) {
        if (!g_IsCalculated) { ShowError(hWnd, WarnNoCalc); return; }
        bool isA = (wmId == ID_BTN_VIS_A); HBITMAP& bmp = isA ? g_hPlotBitmapA : g_hPlotBitmapB;
        if (!bmp) {
            g_StatusMessage = isA ? L"⏳ Рендеринг Поля А..." : L"⏳ Рендеринг Поля Б...";
            g_ActiveView = 5; InvalidateRect(hWnd, NULL, TRUE); UpdateWindow(hWnd);
            ErrCode err;
            if (!GenerateWolframPlot(isA, err)) {
                g_StatusMessage = L"❌ Ошибка рендеринга!"; g_ActiveView = 2;
                InvalidateRect(hWnd, NULL, TRUE); UpdateWindow(hWnd);
                ShowError(hWnd, err); return;
            }
        }
        g_StatusMessage = isA ? L"✔ Визуализация Поля А готова!" : L"✔ Визуализация Поля Б готова!";
        g_ActiveView = isA ? 3 : 4;
    }
}

// 37. Модуль HandleWmCommand
void HandleWmCommand(HWND hWnd, WPARAM wParam) {
    int wmId = LOWORD(wParam);
    if (wmId == ID_BTN_IMPORT || wmId == ID_BTN_SHOW) HandleCmdImportShow(hWnd, wmId);
    else if (wmId == ID_BTN_CALC || wmId == ID_BTN_EXPORT) HandleCmdCalcExport(hWnd, wmId);
    else if (wmId == ID_BTN_VIS_A || wmId == ID_BTN_VIS_B) HandleCmdVis(hWnd, wmId);
    InvalidateRect(hWnd, NULL, TRUE);
}

// 38. Модуль WndProc
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: CreateInterfaceButtons(hWnd, ((LPCREATESTRUCT)lParam)->hInstance); break;
    case WM_COMMAND: HandleWmCommand(hWnd, wParam); break;
    case WM_PAINT: HandlePaint(hWnd); break;
    case WM_DESTROY:
        if (g_hPlotBitmapA) DeleteObject(g_hPlotBitmapA);
        if (g_hPlotBitmapB) DeleteObject(g_hPlotBitmapB);
        PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// 39. Модуль WinMain
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPInst, _In_ LPSTR cmd, _In_ int nCmdShow) {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInst, NULL, LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), NULL, L"FRC", NULL };
    if (!RegisterClassExW(&wcex)) return 0;
    HWND hWnd = CreateWindowW(L"FRC", L"Теория Поля — Расчет параметров векторных полей", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, nCmdShow); UpdateWindow(hWnd);
    MSG msg; while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return (int)msg.wParam;
}

// 40. Модуль PolyToWolframString
wstring PolyToWolframString(const Polynomial& p) {
    if (p.terms.empty()) return L"0";
    wstringstream ss; bool first = true;
    for (const auto& term : p.terms) {
        if (term.coef > 0 && !first) ss << L" + ";
        if (term.coef < 0) ss << (first ? L"-" : L" - ");
        ss << fixed << setprecision(4) << abs(term.coef);
        if (term.px > 0) { ss << L"*x"; if (term.px > 1) ss << L"^" << term.px; }
        if (term.py > 0) { ss << L"*y"; if (term.py > 1) ss << L"^" << term.py; }
        if (term.pz > 0) { ss << L"*z"; if (term.pz > 1) ss << L"^" << term.pz; }
        first = false;
    }
    return ss.str();
}

// 41. Модуль GenerateWolframPlot
bool GenerateWolframPlot(bool isA, ErrCode& err) {
    wstring px = PolyToWolframString(isA ? FieldMathCalculations::fieldA_X : FieldMathCalculations::GetFieldBX());
    wstring py = PolyToWolframString(isA ? FieldMathCalculations::fieldA_Y : FieldMathCalculations::GetFieldBY());
    wstring pz = PolyToWolframString(isA ? FieldMathCalculations::fieldA_Z : FieldMathCalculations::GetFieldBZ());
    wstring scr = isA ? L"t_a.wl" : L"t_b.wl", bmp = isA ? L"t_a.bmp" : L"t_b.bmp";
    DeleteFileW(bmp.c_str());
    if (!WriteScript(isA, px, py, pz, scr, bmp)) { err = ErrWlScript; return false; }
    if (!RunWolfram(scr)) { err = ErrWlNotFound; return false; }
    HBITMAP hBmp = (HBITMAP)LoadImageW(NULL, bmp.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    if (!hBmp) { err = ErrWlImage; return false; }
    if (isA) g_hPlotBitmapA = hBmp; else g_hPlotBitmapB = hBmp;
    return true;
}

// 42. Модуль WriteScript
bool WriteScript(bool isA, const wstring& px, const wstring& py, const wstring& pz, const wstring& scr, const wstring& bmp) {
    wstring pot = isA ? L"Norm[f]" : PolyToWolframString(FieldMathCalculations::GetAnalyticalPotential());
    wstringstream ss;
    ss << L"f={" << px << L"," << py << L"," << pz << L"};\npot=" << pot << L";\n"
        << L"v=VectorPlot3D[f,{x,-2,2},{y,-2,2},{z,-2,2},VectorPoints->{7,7,7},VectorColorFunction->\"Rainbow\",VectorStyle->\"Arrow\",VectorSizes->0.7,VectorAspectRatio->0.1,ViewPoint->{2.5,-2.5,1.8}];\n"
        << L"s=ContourPlot3D[pot,{x,-2,2},{y,-2,2},{z,-2,2},Contours->1,ColorFunction->\"Rainbow\",ContourStyle->Directive[Opacity[0.7]],Mesh->None,ViewPoint->{2.5,-2.5,1.8}];\n"
        << L"combined=GraphicsRow[{s,v},ImageSize->1600];\nExport[\"" << bmp << L"\",combined,ImageResolution->150];\n";
    wstring ws = ss.str();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string u8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &u8[0], len, nullptr, nullptr);
    ofstream f(scr, ios::binary);
    if (!f.is_open()) return false;
    f.write(u8.c_str(), len - 1);
    return true;
}

// 43. Модуль RunWolfram
bool RunWolfram(const wstring& scr) {
    wstring cmd = L"\"C:\\Program Files\\Wolfram Research\\WolframScript\\wolframscript.exe\" -file " + scr;
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
}

// 44. Модуль DrawVisualization
void DrawVisualization(HDC hdc, RECT rect) {
    HBITMAP hBmp = (g_ActiveView == 3) ? g_hPlotBitmapA : g_hPlotBitmapB;
    if (!hBmp) return;
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);
    BITMAP bitmap; GetObject(hBmp, sizeof(BITMAP), &bitmap);
    int lblH = 35, dW, dH, dX, dY;
    GetDrawCoords(bitmap, rect, lblH, dW, dH, dX, dY);
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(hdc, dX, dY, dW, dH, hdcMem, 0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
    SelectObject(hdcMem, hOldBmp); DeleteDC(hdcMem);
    DrawLabels(hdc, dX, dW, dY, dH, lblH);
}

// 45. Модуль GetDrawCoords
void GetDrawCoords(const BITMAP& bmp, const RECT& r, int lblH, int& dW, int& dH, int& dX, int& dY) {
    int rW = r.right - r.left, rH = r.bottom - r.top - lblH;
    if (rH < 50) rH = 50;
    double aspB = (double)bmp.bmWidth / bmp.bmHeight, aspR = (double)rW / rH;
    if (aspB > aspR) {
        dW = rW; dH = (int)(rW / aspB);
        dX = r.left; dY = r.top + (rH - dH) / 2;
    }
    else {
        dW = (int)(rH * aspB); dH = rH;
        dX = r.left + (rW - dW) / 2; dY = r.top;
    }
}

// 46. Модуль DrawLabels
void DrawLabels(HDC hdc, int dX, int dW, int dY, int dH, int lblH) {
    wstring lL = (g_ActiveView == 3) ? L"Поле A: эквипотенциальные поверхности" : L"Поле B: эквипотенциальные поверхности";
    wstring lR = (g_ActiveView == 3) ? L"Поле A: силовые линии" : L"Поле B: силовые линии";
    HFONT hF = CreateFontW(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT hOldF = (HFONT)SelectObject(hdc, hF);
    int oldBk = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldCol = SetTextColor(hdc, RGB(50, 50, 50));
    int lY = dY + dH + 5;
    RECT rL = { dX, lY, dX + dW / 2, lY + lblH };
    RECT rR = { dX + dW / 2, lY, dX + dW, lY + lblH };
    DrawTextW(hdc, lL.c_str(), -1, &rL, DT_CENTER | DT_TOP | DT_SINGLELINE);
    DrawTextW(hdc, lR.c_str(), -1, &rR, DT_CENTER | DT_TOP | DT_SINGLELINE);
    SetTextColor(hdc, oldCol); SetBkMode(hdc, oldBk);
    SelectObject(hdc, hOldF); DeleteObject(hF);
}

// 47. Модуль ShowError
void ShowError(HWND hWnd, ErrCode code) {
    const wchar_t* m = L"Неизвестная ошибка"; const wchar_t* t = L"Ошибка"; UINT ic = MB_ICONERROR;
    if (code == ErrParse) { m = L"Файл поврежден, пуст или имеет неверный формат!"; t = L"Ошибка парсинга"; }
    else if (code == WarnNoData) { m = L"Сперва необходимо загрузить данные!"; t = L"Предупреждение"; ic = MB_ICONWARNING; }
    else if (code == WarnNoCalc) { m = L"Сперва необходимо провести расчеты!"; t = L"Предупреждение"; ic = MB_ICONWARNING; }
    else if (code == ErrWrite) { m = L"Ошибка записи файла!"; }
    else if (code == ErrWlScript) { m = L"Не удалось создать файл скрипта (.wl)! Проверьте права."; t = L"Ошибка визуализации"; }
    else if (code == ErrWlNotFound) { m = L"WolframScript не найден!\nПроверьте путь к wolframscript.exe в коде."; t = L"Ошибка визуализации"; }
    else if (code == ErrWlImage) { m = L"Изображение не создано!\nВозможно, скрипт Wolfram завершился с ошибкой."; t = L"Ошибка визуализации"; }
    MessageBoxW(hWnd, m, t, ic | MB_OK);
}