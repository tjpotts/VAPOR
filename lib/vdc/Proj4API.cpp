#include <iostream>
#include <proj.h>
#include <vapor/ResourcePath.h>
#include <vapor/Proj4API.h>

using namespace VAPoR;
using namespace Wasp;

Proj4API::Proj4API()
{
    _P = NULL;

    string path = GetSharePath("proj");
    if (!path.empty()) {
#ifdef WIN32
        path = "PROJ_LIB=" + path;
        int rc = _putenv(path.c_str());
        if (rc != 0) MyBase::SetErrMsg("putenv failed on PROJ_LIB setting");
#else
        setenv("PROJ_LIB", path.c_str(), 1);
#endif
    }
}

Proj4API::~Proj4API()
{
    if (_P) proj_destroy(_P);
}

int Proj4API::_Initialize(string srcdef, string dstdef, PJ **P) const
{
    *P = NULL;

    if (!srcdef.empty() && !dstdef.empty()) {
        *P = proj_create_crs_to_crs(PJ_DEFAULT_CTX, srcdef.c_str(), dstdef.c_str(), NULL);
        if (!*P) {
            SetErrMsg("proj_create_crs_to_crs(PJ_DEFAULT_CTX, %s, %s, NULL) : %s", srcdef.c_str(), dstdef.c_str(), ProjErr().c_str());
            return (-1);
        }
    }

    //
    // If either the source or destination definition string is
    // not provided - but not both - generate a "latlong" conversion
    //

    /* TODO: Replace this
    if (srcdef.empty() && !dstdef.empty()) {
        *pjSrc = pj_latlong_from_proj(*pjDst);
        if (!*pjSrc) {
            SetErrMsg("pj_latlong_from_proj() : %s", ProjErr().c_str());
            return (-1);
        }
    } else if (!srcdef.empty() && dstdef.empty()) {
        *pjDst = pj_latlong_from_proj(*pjSrc);
        if (!*pjDst) {
            SetErrMsg("pj_latlong_from_proj() : %s", ProjErr().c_str());
            return (-1);
        }
    } else {
        // NULL transform. Transforms will be no-ops
    }
    */
    return (0);
}

int Proj4API::Initialize(string srcdef, string dstdef)
{
    if (_P) proj_destroy(_P);
    _P = NULL;

    return (_Initialize(srcdef, dstdef, &_P));
}

bool Proj4API::IsLatLonSrc() const
{
  return !IsGeocentSrc();
}

bool Proj4API::IsLatLonDst() const
{
  return !IsGeocentDst();
}

bool Proj4API::IsGeocentSrc() const
{
    PJ *srcP = proj_get_source_crs(PJ_DEFAULT_CTX, _P);
    if (!srcP) return (false);

    PJ_COORDINATE_SYSTEM_TYPE type = proj_cs_get_type(PJ_DEFAULT_CTX, srcP);
    bool is_cartesian = type == PJ_CS_TYPE_CARTESIAN;

    proj_destroy(srcP);
    return is_cartesian;
}

bool Proj4API::IsGeocentDst() const
{
    PJ *dstP = proj_get_target_crs(PJ_DEFAULT_CTX, _P);
    if (!dstP) return (false);

    PJ_COORDINATE_SYSTEM_TYPE type = proj_cs_get_type(PJ_DEFAULT_CTX, dstP);
    bool is_cartesian = type == PJ_CS_TYPE_CARTESIAN;

    proj_destroy(dstP);
    return is_cartesian;
}

string Proj4API::GetSrcStr() const
{
    PJ *srcP = proj_get_source_crs(PJ_DEFAULT_CTX, _P);
    if (!srcP) return ("");

    return ((string)proj_as_proj_string(PJ_DEFAULT_CTX, srcP, PJ_PROJ_5, NULL));
}

string Proj4API::GetDstStr() const
{
    PJ *dstP = proj_get_target_crs(PJ_DEFAULT_CTX, _P);
    if (!dstP) return ("");

    return ((string)proj_as_proj_string(PJ_DEFAULT_CTX, dstP, PJ_PROJ_5, NULL));
}

int Proj4API::Transform(double *x, double *y, size_t n, int offset) const { return (Proj4API::Transform(x, y, NULL, n, offset)); }

int Proj4API::_Transform(PJ *P, double *x, double *y, double *z, size_t n, int offset) const
{
    // no-op
    //
    if (P == NULL) return (0);

    //
    // Convert from degrees to radians if the transformation expects radians input
    //
    if (proj_angular_input(P, PJ_FWD)) {
        if (x) {
            for (size_t i = 0; i < n; i++) { x[i * (size_t)offset] = proj_torad(x[i * (size_t)offset]); }
        }
        if (y) {
            for (size_t i = 0; i < n; i++) { y[i * (size_t)offset] = proj_torad(y[i * (size_t)offset]); }
        }
        if (z) {
            for (size_t i = 0; i < n; i++) { z[i * (size_t)offset] = proj_torad(z[i * (size_t)offset]); }
        }
    }

    int rc = proj_trans_generic(P, PJ_FWD,
                                x, sizeof(double), n,
                                y, sizeof(double), n,
                                z, sizeof(double), n,
                                NULL, 0, 0);

    if (rc == 0) {
        SetErrMsg("proj_trans_generic() : %s", ProjErr().c_str());
        return (-1);
    }

    //
    // Convert from radians to degrees if transformation provides radians output
    //
    if (proj_angular_output(P, PJ_FWD)) {
        if (x) {
            for (size_t i = 0; i < n; i++) { x[i * (size_t)offset] *= proj_todeg(x[i * (size_t)offset]); }
        }
        if (y) {
            for (size_t i = 0; i < n; i++) { y[i * (size_t)offset] = proj_todeg(y[i * (size_t)offset]); }
        }
        if (z) {
            for (size_t i = 0; i < n; i++) { z[i * (size_t)offset] = proj_todeg(z[i * (size_t)offset]); }
        }
    }
    return (0);
}

int Proj4API::Transform(double *x, double *y, double *z, size_t n, int offset) const { return (_Transform(_P, x, y, z, n, offset)); }

int Proj4API::Transform(float *x, float *y, size_t n, int offset) const { return (Proj4API::Transform(x, y, NULL, n, offset)); }

int Proj4API::_Transform(PJ *P, float *x, float *y, float *z, size_t n, int offset) const
{
    double *xd = NULL;
    double *yd = NULL;
    double *zd = NULL;

    if (x) {
        xd = new double[n];
        for (size_t i = 0; i < n; i++) xd[i] = x[i * offset];
    }
    if (y) {
        yd = new double[n];
        for (size_t i = 0; i < n; i++) yd[i] = y[i * offset];
    }
    if (z) {
        zd = new double[n];
        for (size_t i = 0; i < n; i++) zd[i] = z[i * offset];
    }

    int rc = _Transform(P, xd, yd, zd, n, 1);

    if (xd) {
        for (size_t i = 0; i < n; i++) x[i * offset] = xd[i];
        delete[] xd;
    }
    if (yd) {
        for (size_t i = 0; i < n; i++) y[i * offset] = yd[i];
        delete[] yd;
    }
    if (zd) {
        for (size_t i = 0; i < n; i++) z[i * offset] = zd[i];
        delete[] zd;
    }
    return (rc);
}

int Proj4API::Transform(float *x, float *y, float *z, size_t n, int offset) const { return (Proj4API::_Transform(_P, x, y, z, n, offset)); }

int Proj4API::Transform(string srcdef, string dstdef, double *x, double *y, double *z, size_t n, int offset) const
{
    PJ *P = NULL;

    int rc = _Initialize(srcdef, dstdef, &P);
    if (rc < 0) return (rc);

    return (_Transform(P, x, y, z, n, offset));

    return (0);
}

int Proj4API::Transform(string srcdef, string dstdef, float *x, float *y, float *z, size_t n, int offset) const
{
    PJ *P = NULL;

    int rc = _Initialize(srcdef, dstdef, &P);
    if (rc < 0) return (rc);

    return (_Transform(P, x, y, z, n, offset));

    return (0);
}

string Proj4API::ProjErr() const { return (proj_context_errno_string(PJ_DEFAULT_CTX, proj_context_errno(PJ_DEFAULT_CTX))); }

void Proj4API::Clamp(double *x, double *y, size_t n, int offset) const
{
    double minx, miny, maxx, maxy;

    string projstring = GetSrcStr();

    if (IsLatLonSrc()) {
        minx = -180.0;
        miny = -90.0;
        maxx = 180.0;
        maxy = 90.0;
    } else if (std::string::npos != projstring.find("proj=eqc")) {
        minx = -20037508.3427892;
        miny = -10018754.1713946;
        maxx = -minx;
        maxy = -miny;
    } else if (std::string::npos != projstring.find("proj=merc")) {
        minx = -20037508.340;
        miny = minx;
        maxx = -minx;
        maxy = -miny;
    } else {
        return;    // unknown  projectoin
    }

    for (int i = 0; i < n; i++) {
        if (x[i * offset] < minx) x[i * offset] = minx;
        if (y[i * offset] < miny) y[i * offset] = miny;
        if (x[i * offset] > maxx) x[i * offset] = maxx;
        if (y[i * offset] > maxy) y[i * offset] = maxy;
    }
}

bool Proj4API::IsCylindrical() const
{
    string proj4String = GetDstStr();

    return ((proj4String.find("+proj=eqc") != std::string::npos) || (proj4String.find("+proj=merc") != std::string::npos));
}
