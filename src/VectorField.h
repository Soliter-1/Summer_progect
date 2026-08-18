#pragma once

#include <string>
#include <vector>
#include <functional>
#include "resource.h"

using namespace std;

// ============================================================================
// Œ¡⁄ﬂ¬À≈Õ»ﬂ —“–” “”– »  À¿——Œ¬
// ============================================================================
struct Vector3D {
    double x, y, z;
};

struct Monomial {
    double coef;
    int px, py, pz;
};

class Polynomial {
public:
    vector<Monomial> terms;
    Polynomial(initializer_list<Monomial> init);
    Polynomial();
    Polynomial SetVariableToZero(bool zeroX, bool zeroY, bool zeroZ) const;
    Polynomial IntegrateSymbolically(int varIndex) const;
    void Add(const Polynomial& other);
    double Evaluate(double x, double y, double z) const;
    wstring ToWString() const;
};

class FieldMathCalculations {
public:
    typedef function<double(double, double, double)> FUNC3D;
    typedef function<Vector3D(double, double, double)> FUNC3D_VEC;
    static Polynomial fieldA_X, fieldA_Y, fieldA_Z;
    static Polynomial fieldB_X, fieldB_Y, fieldB_Z;
    static void ParsePolynomials(const wstring& a_str, const wstring& b_str);
    static Vector3D EvaluateFieldA(double x, double y, double z);
    static Polynomial GetFieldBX();
    static Polynomial GetFieldBY();
    static Polynomial GetFieldBZ();
    static Vector3D EvaluateFieldB(double x, double y, double z);
    static double Calc_PartialDerivative(FUNC3D f, int variable, double x, double y, double z);
    static double IntegrateQuarterDisk(function<double(double, double)> func, double radius = 1.0, int steps = 50);
    static double Calc_Divergence(FUNC3D_VEC field, double x, double y, double z);
    static Vector3D Calc_Rotor(FUNC3D_VEC field, double x, double y, double z);
    static wstring DetermineFieldClass(FUNC3D_VEC field);
    static double Calc_Flux_GaussOstrogradsky();
    static double Calc_Flux_Direct();
    static double Calc_Circulation_Stokes();
    static double Calc_Circulation_Direct();
    static Polynomial GetAnalyticalPotential();
    static bool VerifyPotentialCorrectness();
    static bool CheckRotorBZero();
private:
    static Polynomial ParsePoly(wstring expr);
    static void ExtractComponents(wstring line, Polynomial& P, Polynomial& Q, Polynomial& R);
    static void ReplaceAll(wstring& str, const wstring& from, const wstring& to);
};