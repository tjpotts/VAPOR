#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>
#include "vapor/VAssert.h"
#include <netcdf.h>
#include <vapor/NetCDFCollection.h>

using namespace VAPoR;
using namespace Wasp;
using namespace std;

namespace {

const string derivedTimeDimName = "derivedTimeDim";

template<class T> vector<T> make_container_unique(vector<T> v)
{
    sort(v.begin(), v.end());
    auto last2 = unique(v.begin(), v.end());
    v.erase(last2, v.end());
    return (v);
}


};    // namespace

NetCDFCollection::NetCDFCollection()
{
    _variableList.clear();
    _dimNames.clear();
    _dimLens.clear();
    _missingValAttName.clear();
    _times.clear();
    _timesMap.clear();
    _ovr_table.clear();
    _ncdfmap.clear();
    _failedVars.clear();
}

NetCDFCollection::~NetCDFCollection() { ReInitialize(); }

void NetCDFCollection::ReInitialize()
{
    map<string, NetCDFSimple *>::iterator itr;
    for (itr = _ncdfmap.begin(); itr != _ncdfmap.end(); ++itr) { delete itr->second; }

    std::map<int, fileHandle>::iterator itr1;
    for (itr1 = _ovr_table.begin(); itr1 != _ovr_table.end(); ++itr1) {
        int fd = itr1->first;
        (void)NetCDFCollection::Close(fd);
    }

    _variableList.clear();
    _dimNames.clear();
    _dimLens.clear();
    _missingValAttName.clear();
    _times.clear();
    _timesMap.clear();
    _ovr_table.clear();
    _ncdfmap.clear();
    _failedVars.clear();
}

int NetCDFCollection::Initialize(const vector<string> &files, const vector<string> &time_dimnames, const vector<string> &time_coordvars)
{
    vector<string> l_time_dimnames = time_dimnames;

    ReInitialize();

    //
    // Build a hash table to map a variable's time dimension
    // to its time coordinates
    //
    int                 file_org;    // case 1, 2, 3 (3a or 3b)
    map<string, size_t> timeDimLens;
    int                 rc = NetCDFCollection::_InitializeTimesMap(files, l_time_dimnames, time_coordvars, _timesMap, timeDimLens, _times, file_org);
    if (rc < 0) return (-1);

    for (auto itr : timeDimLens) {
        _dimNames.push_back(itr.first);
        _dimLens.push_back(itr.second);
        _dimIsTimeVarying.push_back(false);
    }

    for (int i = 0; i < files.size(); i++) {
        NetCDFSimple *netcdf = new NetCDFSimple();
        _ncdfmap[files[i]] = netcdf;

        rc = netcdf->Initialize(files[i]);
        if (rc < 0) {
            SetErrMsg("NetCDFSimple::Initialize(%s)", files[i].c_str());
            return (-1);
        }
        //        printf("INIT %i = %s\n", i, files[i].c_str());

        //
        // Get dimension names and lengths
        //
        vector<string> dimnames;
        vector<size_t> dims;
        netcdf->GetDimensions(dimnames, dims);
        for (int j = 0; j < dimnames.size(); j++) {
            // Handle time dims separately
            //
            if (timeDimLens.find(dimnames[j]) != timeDimLens.end()) continue;

            // No duplicates
            //
            vector<string>::iterator itr;
            itr = find(_dimNames.begin(), _dimNames.end(), dimnames[j]);
            if (itr == _dimNames.end()) {
                _dimNames.push_back(dimnames[j]);
                _dimLens.push_back(dims[j]);
                _dimIsTimeVarying.push_back(false);
            } else if (_dimLens[itr - _dimNames.begin()] != dims[j]) {
                _dimIsTimeVarying[itr - _dimNames.begin()] = true;
            }
        }

        //
        // Get variable info for all variables in current file
        //
        const vector<NetCDFSimple::Variable> &variables = netcdf->GetVariables();

        //
        // For each variable in the file add it to _variablesList
        //
        for (int j = 0; j < variables.size(); j++) {
            string name = variables[j].GetName();

            map<string, TimeVaryingVar>::iterator p = _variableList.find(name);

            //
            // If this is the first time we've seen this variable
            // add it to _variablesList
            //
            if (p == _variableList.end()) {
                TimeVaryingVar tvv;
                _variableList[name] = tvv;
                p = _variableList.find(name);
            }
            TimeVaryingVar &tvvref = p->second;

            bool enable = EnableErrMsg(false);
            int  rc = tvvref.Insert(netcdf, variables[j], files[i], l_time_dimnames, _timesMap, file_org);
            (void)EnableErrMsg(enable);
            if (rc < 0) {
                SetErrCode(0);
                _failedVars.push_back(files[i] + ": " + variables[j].GetName());
                continue;
            }
        }
    }

    for (auto itr = _variableList.begin(); itr != _variableList.end(); ++itr) {
        TimeVaryingVar &tvvref = itr->second;
        tvvref.Sort();
    }

    return (0);
}

#include <vapor/STLUtils.h>

long NetCDFCollection::GetDimLengthAtTime(string name, long ts)
{
    double realTime = _times[ts];

    const auto end = _timesMap.cend();
    string     filePath;
    for (auto it = _timesMap.cbegin(); it != end; ++it) {
        for (int i = 0; i < it->second.size(); i++) {
            if (it->second[i] == realTime) {
                filePath = it->first;
                goto SEARCH_FINISHED;
            }
        }
    }
SEARCH_FINISHED:

    if (filePath.empty()) {
        MyBase::SetErrMsg("Time %li (%f) not found", ts, realTime);
        assert(0);
        return -1;
    }

    NetCDFSimple *nc = nullptr;
    for (auto it = _ncdfmap.cbegin(); it != _ncdfmap.cend(); ++it) {
        if (STLUtils::BeginsWith(filePath, it->first)) {
            nc = it->second;
            break;
        }
    }

    if (!nc) {
        MyBase::SetErrMsg("NC for file not found");
        assert(0);
        return -1;
    }

    vector<string> names;
    vector<size_t> lengths;
    nc->GetDimensions(names, lengths);

    for (int i = 0; i < names.size(); i++) {
        if (names[i] == name) { return lengths[i]; }
    }

    MyBase::SetErrMsg("Dimension not found at timestep %li", ts);
    return -1;
}

bool NetCDFCollection::VariableExists(string varname) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (false); }
    return (true);
}

bool NetCDFCollection::VariableExists(size_t ts, string varname) const
{
    if (ts >= _times.size()) return (false);

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (false); }
    const TimeVaryingVar &tvvar = p->second;

    if (!tvvar.GetTimeVarying()) return (true);    // CV variables exist for all times

    double mytime = _times[ts];

    vector<double> times = tvvar.GetTimes();
    for (int i = 0; i < times.size(); i++) {
        if (times[i] == mytime) return (true);
    }
    return (false);
}

vector<string> NetCDFCollection::GetVariableNames(int ndims, bool spatial) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.begin();

    vector<string> names;

    for (; p != _variableList.end(); ++p) {
        const TimeVaryingVar &tvvars = p->second;
        int                   myndims = tvvars.GetSpatialDims().size();
        if (!spatial && tvvars.GetTimeVarying()) { myndims++; }
        if (myndims == ndims) { names.push_back(p->first); }
    }

    return (names);
}

vector<size_t> NetCDFCollection::GetSpatialDims(string varname) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (vector<size_t>()); }
    const TimeVaryingVar &tvvars = p->second;

    return (tvvars.GetSpatialDims());
}

vector<string> NetCDFCollection::GetSpatialDimNames(string varname) const
{
    vector<string> dimnames;

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (dimnames); }
    const TimeVaryingVar &tvvars = p->second;

    dimnames = tvvars.GetSpatialDimNames();
    return (dimnames);
}

size_t NetCDFCollection::GetTimeDim(string varname) const
{

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (0); }
    const TimeVaryingVar &tvvars = p->second;

    return (tvvars.GetNumTimeSteps());
}

string NetCDFCollection::GetTimeDimName(string varname) const
{
    string dimname;

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (dimname); }
    const TimeVaryingVar &tvvars = p->second;

    dimname = tvvars.GetTimeDimName();
    return (dimname);
}

vector<size_t> NetCDFCollection::GetDims(string varname) const
{
    std::vector<size_t> dims = NetCDFCollection::GetSpatialDims(varname);
    if (NetCDFCollection::IsTimeVarying(varname)) { dims.insert(dims.begin(), NetCDFCollection::GetTimeDim(varname)); }
    return (dims);
}

vector<string> NetCDFCollection::GetDimNames(string varname) const
{
    std::vector<string> dimnames = NetCDFCollection::GetSpatialDimNames(varname);
    if (NetCDFCollection::IsTimeVarying(varname)) { dimnames.insert(dimnames.begin(), NetCDFCollection::GetTimeDimName(varname)); }
    return (dimnames);
}

bool NetCDFCollection::IsTimeVarying(string varname) const
{

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (false); }
    const TimeVaryingVar &tvvars = p->second;
    return (tvvars.GetTimeVarying());
}

int NetCDFCollection::GetXType(string varname) const
{

    NetCDFSimple::Variable varinfo;
    int                    rc = NetCDFCollection::GetVariableInfo(varname, varinfo);
    if (rc < 0) return (-1);

    return (varinfo.GetXType());
}

std::vector<string> NetCDFCollection::GetAttNames(string varname) const
{
    std::vector<string> attnames;

    //
    // See if global attribute
    //
    if (varname.empty()) {
        if (_ncdfmap.empty()) { return (attnames); }
        NetCDFSimple *netcdf = _ncdfmap.begin()->second;
        return (netcdf->GetAttNames());
    }

    NetCDFSimple::Variable varinfo;
    bool                   ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) { return (attnames); }

    return (varinfo.GetAttNames());
}

int NetCDFCollection::GetAttType(string varname, string attname) const
{
    //
    // See if global attribute
    //
    if (varname.empty()) {
        if (_ncdfmap.empty()) { return (-1); }
        NetCDFSimple *netcdf = _ncdfmap.begin()->second;
        return (netcdf->GetAttType(attname));
    }

    NetCDFSimple::Variable varinfo;
    bool                   ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) { return (-1); }

    return (varinfo.GetAttType(attname));
}

void NetCDFCollection::GetAtt(string varname, string attname, std::vector<double> &values) const
{
    values.clear();

    //
    // See if global attribute
    //
    if (varname.empty()) {
        if (_ncdfmap.empty()) { return; }
        NetCDFSimple *netcdf = _ncdfmap.begin()->second;
        netcdf->GetAtt(attname, values);
        return;
    }

    NetCDFSimple::Variable varinfo;
    bool                   ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) { return; }
    varinfo.GetAtt(attname, values);
}

void NetCDFCollection::GetAtt(string varname, string attname, std::vector<long> &values) const
{
    values.clear();

    //
    // See if global attribute
    //
    if (varname.empty()) {
        if (_ncdfmap.empty()) { return; }
        NetCDFSimple *netcdf = _ncdfmap.begin()->second;
        netcdf->GetAtt(attname, values);
        return;
    }

    NetCDFSimple::Variable varinfo;
    bool                   ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) { return; }
    varinfo.GetAtt(attname, values);
}

void NetCDFCollection::GetAtt(string varname, string attname, string &values) const
{
    values.clear();

    //
    // See if global attribute
    //
    if (varname.empty()) {
        if (_ncdfmap.empty()) { return; }
        NetCDFSimple *netcdf = _ncdfmap.begin()->second;
        netcdf->GetAtt(attname, values);
        return;
    }

    NetCDFSimple::Variable varinfo;
    bool                   ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) { return; }
    varinfo.GetAtt(attname, values);
}

int NetCDFCollection::GetTime(size_t ts, double &time) const
{
    time = 0.0;
    if (ts >= _times.size()) {
        SetErrMsg("Invalid time step: %d", ts);
        return (-1);
    }
    time = _times[ts];
    return (0);
}

int NetCDFCollection::GetTimes(string varname, vector<double> &times) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) {
        SetErrMsg("Invalid variable \"%s\"", varname.c_str());
        return (-1);
    }
    const TimeVaryingVar &tvvars = p->second;
    if (!tvvars.GetTimeVarying()) {
        times = _times;    // CV variables defined for all times
    } else {
        times = tvvars.GetTimes();
    }
    return (0);
}

int NetCDFCollection::GetFile(size_t ts, string varname, string &file, size_t &local_ts) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) {
        SetErrMsg("Invalid variable \"%s\"", varname.c_str());
        return (-1);
    }
    const TimeVaryingVar &tvvars = p->second;

    double time;
    int    rc = GetTime(ts, time);
    if (rc < 0) return (-1);

    size_t var_ts;
    rc = tvvars.GetTimeStep(time, var_ts);
    if (rc < 0) return (-1);

    local_ts = tvvars.GetLocalTimeStep(var_ts);

    return (tvvars.GetFile(var_ts, file));
}

bool NetCDFCollection::_GetVariableInfo(string varname, NetCDFSimple::Variable &varinfo) const
{

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) { return (false); }
    const TimeVaryingVar &tvvars = p->second;
    tvvars.GetVariableInfo(varinfo);

    vector<string> dimnames = tvvars.GetSpatialDimNames();
    if (!(tvvars.GetTimeDimName().empty())) { dimnames.insert(dimnames.begin(), tvvars.GetTimeDimName()); }
    varinfo.SetDimNames(dimnames);

    return (true);
}

int NetCDFCollection::GetVariableInfo(string varname, NetCDFSimple::Variable &varinfo) const
{
    bool ok = NetCDFCollection::_GetVariableInfo(varname, varinfo);
    if (!ok) {
        SetErrMsg("Invalid variable \"%s\"", varname.c_str());
        return (-1);
    }

    return (0);
}

bool NetCDFCollection::GetMissingValue(string varname, double &mv) const
{
    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) {
        SetErrMsg("Invalid variable \"%s\"", varname.c_str());
        return (false);
    }
    const TimeVaryingVar &tvvars = p->second;

    return (tvvars.GetMissingValue(_missingValAttName, mv));
}

int NetCDFCollection::OpenRead(size_t ts, string varname)
{
    //
    // Find a file descriptor. Use lowest available, starting with zero
    //
    int fd;
    for (fd = 0; fd < _ovr_table.size(); fd++) {
        if (_ovr_table.find(fd) == _ovr_table.end()) { break; }
    }
    fileHandle fh;

    _ovr_table[fd] = fh;

    map<string, TimeVaryingVar>::const_iterator p = _variableList.find(varname);
    if (p == _variableList.end()) {
        SetErrMsg("Invalid variable \"%s\"", varname.c_str());
        return (-1);
    }
    const TimeVaryingVar &tvvars = p->second;

    double time;
    int    rc = GetTime(ts, time);
    if (rc < 0) return (-1);

    size_t var_ts;
    rc = tvvars.GetTimeStep(time, var_ts);
    if (rc < 0) return (-1);

    fh._tvvars = tvvars;
    fh._local_ts = fh._tvvars.GetLocalTimeStep(var_ts);
    fh._slice = 0;
    fh._first_slice = true;

    string                 path;
    NetCDFSimple::Variable varinfo;
    fh._tvvars.GetFile(var_ts, path);
    fh._tvvars.GetVariableInfo(varinfo);

    fh._has_missing = fh._tvvars.GetMissingValue(_missingValAttName, fh._missing_value);

    fh._ncdfptr = _ncdfmap[path];
    fh._fd = fh._ncdfptr->OpenRead(varinfo);
    if (fh._fd < 0) {
        SetErrMsg("NetCDFCollection::OpenRead(%d, %s) : failed", var_ts, varname.c_str());
        return (-1);
    }

    _ovr_table[fd] = fh;
    return (fd);
}

template<typename T> int NetCDFCollection::_read_template(size_t start[], size_t count[], T *data, int fd)
{
    size_t mystart[NC_MAX_VAR_DIMS];
    size_t mycount[NC_MAX_VAR_DIMS];

    std::map<int, fileHandle>::iterator itr;
    if ((itr = _ovr_table.find(fd)) == _ovr_table.end()) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return (-1);
    }
    fileHandle &fh = itr->second;

    int idx = 0;
    if (fh._tvvars.GetTimeVarying() && !(fh._tvvars.GetTimeDimName().empty() || fh._tvvars.GetTimeDimName() == derivedTimeDimName)) {
        mystart[idx] = fh._local_ts;
        mycount[idx] = 1;
        idx++;
    }
    for (int i = 0; i < fh._tvvars.GetSpatialDims().size(); i++) {
        mystart[idx] = start[i];
        mycount[idx] = count[i];
        idx++;
    }

    return (fh._ncdfptr->Read(mystart, mycount, data, fh._fd));
}

int NetCDFCollection::Read(size_t start[], size_t count[], double *data, int fd) { return (_read_template(start, count, data, fd)); }
int NetCDFCollection::Read(size_t start[], size_t count[], float *data, int fd) { return (_read_template(start, count, data, fd)); }
int NetCDFCollection::Read(size_t start[], size_t count[], int *data, int fd) { return (_read_template(start, count, data, fd)); }
int NetCDFCollection::Read(size_t start[], size_t count[], char *data, int fd) { return (_read_template(start, count, data, fd)); }


int NetCDFCollection::_InitializeTimesMap(const vector<string> &files, const vector<string> &time_dimnames, const vector<string> &time_coordvars, map<string, vector<double>> &timesMap,
                                          map<string, size_t> &timeDimLens, vector<double> &times, int &file_org) const
{
    timesMap.clear();
    timeDimLens.clear();
    if (time_coordvars.size() && (time_coordvars.size() != time_dimnames.size())) {
        SetErrMsg("NetCDFCollection::Initialize() : number of time coordinate variables and time dimensions must match when time coordinate variables specified");
        return (-1);
    }

    int rc;
    if (time_dimnames.size() == 0) {
        file_org = 1;
        rc = _InitializeTimesMapCase1(files, timesMap, timeDimLens);
    } else if ((time_dimnames.size() != 0) && (time_coordvars.size() == 0)) {
        file_org = 2;
        rc = _InitializeTimesMapCase2(files, time_dimnames, timesMap, timeDimLens);
    } else {
        file_org = 3;
        rc = _InitializeTimesMapCase3(files, time_dimnames, time_coordvars, timesMap, timeDimLens);
    }
    if (rc < 0) return (rc);

    //
    // Generate times: a single vector of all the time coordinates
    //
    map<string, vector<double>>::const_iterator itr1;
    for (itr1 = timesMap.begin(); itr1 != timesMap.end(); ++itr1) {
        const vector<double> &timesref = itr1->second;
        times.insert(times.end(), timesref.begin(), timesref.end());
    }

    //
    // sort and remove duplicates
    //
    times = make_container_unique(times);

    if (times.empty()) { times.push_back(0.0); }

    //
    // Create an entry for constant variables, which are defined at all times
    //
    timesMap["constant"] = times;
    return (0);
}

int NetCDFCollection::_InitializeTimesMapCase1(const vector<string> &files, map<string, vector<double>> &timesMap, map<string, size_t> &timeDimLens) const
{
    timesMap.clear();

    // If only on file and no time dimensions than there is no
    //
    if (files.size() < 2) return (0);

    map<string, double> currentTime;    // current time for each variable

    // Case 1: No TDs or TCVs => synthesize TCV
    // A variable's time coordinate is determined by the ordering
    // of the file that it occurs in. The timesMap key is the
    // concatentation of file name (where the variable occurs) and
    // variable name. The only type of variables present are ITVV
    //

    for (int i = 0; i < files.size(); i++) {
        NetCDFSimple *netcdf = new NetCDFSimple();

        int rc = netcdf->Initialize(files[i]);
        if (rc < 0) {
            SetErrMsg("NetCDFSimple::Initialize(%s)", files[i].c_str());
            return (-1);
        }

        const vector<NetCDFSimple::Variable> &variables = netcdf->GetVariables();

        for (int j = 0; j < variables.size(); j++) {
            //
            // Skip 0D variables
            //
            if (variables[j].GetDimNames().size() == 0) continue;

            string varname = variables[j].GetName();

            // If first time this variable has been seen
            // initialize the currentTime
            //
            if (currentTime.find(varname) == currentTime.end()) { currentTime[varname] = 0.0; }

            string         key = files[i] + varname;
            vector<double> times(1, currentTime[varname]);
            timesMap[key] = times;

            currentTime[varname] += 1.0;
        }
        delete netcdf;
    }
    timeDimLens[derivedTimeDimName] = files.size();
    return (0);
}

int NetCDFCollection::_InitializeTimesMapCase2(const vector<string> &files, const vector<string> &time_dimnames, map<string, vector<double>> &timesMap, map<string, size_t> &timeDimLens) const
{
    timesMap.clear();
    map<string, double>         currentTime;    // current time for each variable
    map<string, vector<double>> timeDimTimes;

    // Case 2: TD specified, but no TCV.
    // A variable's time coordinate is determined by the ordering
    // of the file that it occurs in, offset by its time dimesion.
    // The timesMap key is the
    // concatentation of file name (where the variable occurs) and
    // variable name.
    // Both TVV and CV variables may be present.
    //

    for (int i = 0; i < files.size(); i++) {
        NetCDFSimple *netcdf = new NetCDFSimple();

        int rc = netcdf->Initialize(files[i]);
        if (rc < 0) {
            SetErrMsg("NetCDFSimple::Initialize(%s)", files[i].c_str());
            return (-1);
        }

        const vector<NetCDFSimple::Variable> &variables = netcdf->GetVariables();

        for (int j = 0; j < variables.size(); j++) {
            //
            // Skip 0D variables
            //
            if (variables[j].GetDimNames().size() == 0) continue;

            string varname = variables[j].GetName();
            string key = files[i] + varname;
            string timedim = variables[j].GetDimNames()[0];

            // If this is a CV variable (no time dimension) we skip it
            //
            if (find(time_dimnames.begin(), time_dimnames.end(), timedim) == time_dimnames.end()) continue;    // CV variable

            // Number of time steps for this variable
            //
            size_t timedimlen = netcdf->DimLen(timedim);

            // If first time this varname has been seen
            // initialize the currentTime
            //
            if (currentTime.find(varname) == currentTime.end()) { currentTime[varname] = 0.0; }

            vector<double> times;
            for (int t = 0; t < timedimlen; t++) {
                times.push_back(currentTime[varname]);
                currentTime[varname] += 1.0;
            }

            timesMap[key] = times;

            vector<double> &timesref = timeDimTimes[timedim];
            for (int t = 0; t < times.size(); t++) { timesref.push_back(times[t]); }
        }
        delete netcdf;
    }

    for (auto itr : timeDimTimes) {
        vector<double> &ref = itr.second;
        ref = make_container_unique(ref);
        timeDimLens[itr.first] = ref.size();
    }
    return (0);
}

int NetCDFCollection::_InitializeTimesMapCase3(const vector<string> &files, const vector<string> &time_dimnames, const vector<string> &time_coordvars, map<string, vector<double>> &timesMap,
                                               map<string, size_t> &timeDimLens) const
{
    timesMap.clear();

    //
    // tcvcount counts occurrences of each TCV. tcvfile and tcvdim record
    // the last file name and TD pair used to generate hash key for each TCV
    //
    map<string, int>            tcvcount;    // # of files of TCV appears in
    map<string, string>         tcvfile;
    map<string, string>         tcvdim;
    map<string, vector<double>> timeDimTimes;
    for (int i = 0; i < time_coordvars.size(); i++) { tcvcount[time_coordvars[i]] = 0; }

    for (int i = 0; i < files.size(); i++) {
        NetCDFSimple *netcdf = new NetCDFSimple();

        int rc = netcdf->Initialize(files[i]);
        if (rc < 0) return (-1);

        const vector<NetCDFSimple::Variable> &variables = netcdf->GetVariables();

        //
        // For each TCV see if it exists in current file, if so
        // read it and add times to timesMap
        //
        for (int j = 0; j < time_coordvars.size(); j++) {
            int index = _get_var_index(variables, time_coordvars[j]);
            if (index < 0) continue;    // TCV doesn't exist

            // Increment count
            //

            tcvcount[time_coordvars[j]] += 1;

            // Read TCV
            double *buf = _Get1DVar(netcdf, variables[index]);
            if (!buf) {
                SetErrMsg("Failed to read time coordinate variable \"%s\"", time_coordvars[j].c_str());
                return (-1);
            }

            string timedim = variables[index].GetDimNames()[0];
            size_t timedimlen = netcdf->DimLen(timedim);

            vector<double> times;
            for (int t = 0; t < timedimlen; t++) { times.push_back(buf[t]); }
            delete[] buf;

            //
            // The hash key for timesMap is the file plus the
            // time dimension name
            //
            string key = files[i] + timedim;

            // record file and timedim used to generate the hash key
            //
            tcvfile[time_coordvars[j]] = files[i];
            tcvdim[time_coordvars[j]] = timedim;

            if (timesMap.find(key) == timesMap.end()) {
                timesMap[key] = times;
            } else {
                vector<double> &timesref = timesMap[key];
                for (int t = 0; t < times.size(); t++) { timesref.push_back(times[t]); }
            }

            // Map between time dimention names and time coordinates
            //
            vector<double> &timesref = timeDimTimes[timedim];
            for (int t = 0; t < times.size(); t++) { timesref.push_back(times[t]); }
        }

        delete netcdf;
    }

    for (auto itr : timeDimTimes) {
        vector<double> &ref = itr.second;
        ref = make_container_unique(ref);
        timeDimLens[itr.first] = ref.size();
    }

    //
    // Finally, if see if this is case 3a (only one file contains the TCV),
    // or case 3b (a TCV is present in any file containing a TVV).
    // We're only checking for case 3a here. If case 1, replicate the
    // times for each file & time dimension pair
    //
    //

    for (int i = 0; i < time_coordvars.size(); i++) {
        if (tcvcount[time_coordvars[i]] == 1) {
            string                                      key1 = tcvfile[time_coordvars[i]] + tcvdim[time_coordvars[i]];
            map<string, vector<double>>::const_iterator itr;
            for (int j = 0; j < files.size(); j++) {
                string key = files[j] + tcvdim[time_coordvars[i]];
                timesMap[key] = timesMap[key1];
            }
        }
    }

    return (0);
}

double *NetCDFCollection::_Get1DVar(NetCDFSimple *netcdf, const NetCDFSimple::Variable &variable) const
{
    if (variable.GetDimNames().size() != 1) return (NULL);
    int fd = netcdf->OpenRead(variable);
    if (fd < 0) {
        SetErrMsg("Time coordinate variable \"%s\" is invalid", variable.GetName().c_str());
        return (NULL);
    }
    string dimname = variable.GetDimNames()[0];
    size_t dimlen = netcdf->DimLen(dimname);
    size_t start[] = {0};
    size_t count[] = {dimlen};
    double *buf = new double[dimlen];
    int    rc = netcdf->Read(start, count, buf, fd);
    if (rc < 0) { return (NULL); }
    netcdf->Close(fd);
    return (buf);
}

int NetCDFCollection::_get_var_index(const vector<NetCDFSimple::Variable> variables, string varname) const
{
    for (int i = 0; i < variables.size(); i++) {
        if (varname.compare(variables[i].GetName()) == 0) return (i);
    }
    return (-1);
}


template<typename T> int NetCDFCollection::_read_slice_template(T *data, int fd)
{
    std::map<int, fileHandle>::iterator itr;
    if ((itr = _ovr_table.find(fd)) == _ovr_table.end()) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return (-1);
    }
    fileHandle &fh = itr->second;

    const TimeVaryingVar &var = fh._tvvars;
    vector<size_t>        dims = var.GetSpatialDims();

    if (dims.size() < 2 || dims.size() > 3) {
        SetErrMsg("Only 2D and 3D variables supported");
        return (-1);
    }

    size_t nx = dims[dims.size() - 1];
    size_t ny = dims[dims.size() - 2];
    size_t nz = dims.size() > 2 ? dims[dims.size() - 3] : 1;

    if (fh._slice >= nz) return (0);

    size_t start[] = {0, 0, 0};
    size_t count[] = {1, 1, 1};

    if (dims.size() > 2) {
        start[0] = fh._slice;
        count[1] = ny;
        count[2] = nx;
    } else {
        count[0] = ny;
        count[1] = nx;
    }

    int rc = NetCDFCollection::Read(start, count, data, fd);
    fh._slice++;
    if (rc < 0) return (rc);
    return (1);
}

int NetCDFCollection::ReadSlice(float *data, int fd) { return (_read_slice_template(data, fd)); }

int NetCDFCollection::SeekSlice(int offset, int whence, int fd)
{
    std::map<int, fileHandle>::iterator itr;
    if ((itr = _ovr_table.find(fd)) == _ovr_table.end()) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return (-1);
    }
    fileHandle &fh = itr->second;

    if (whence < 0 || whence > 2) {
        SetErrMsg("Invalid whence specification : %d", whence);
        return (-1);
    }

    vector<size_t> dims = fh._tvvars.GetSpatialDims();
    vector<string> dimnames = fh._tvvars.GetSpatialDimNames();

    size_t nz = dims.size() == 3 ? dims[dims.size() - 3] : 1;
    long   nzus = nz;

    int slice = 0;
    if (whence == 0) {
        slice = offset;
    } else if (whence == 1) {
        slice = fh._slice + offset;
    } else if (whence == 2) {
        slice = offset + nzus - 1;
    }
    if (slice < 0) slice = 0;
    if (slice > nzus - 1) slice = nzus - 1;

    fh._slice = slice;
    fh._first_slice = true;
    return (0);
}

int NetCDFCollection::Read(vector<size_t> start, vector<size_t> count, double *data, int fd)
{
    VAssert(start.size() == count.size());

    size_t mystart[NC_MAX_VAR_DIMS];
    size_t mycount[NC_MAX_VAR_DIMS];
    for (int i = 0; i < start.size(); i++) {
        mystart[i] = start[i];
        mycount[i] = count[i];
    }
    return (NetCDFCollection::Read(mystart, mycount, data, fd));
}

int NetCDFCollection::Read(vector<size_t> start, vector<size_t> count, float *data, int fd)
{
    VAssert(start.size() == count.size());

    size_t mystart[NC_MAX_VAR_DIMS];
    size_t mycount[NC_MAX_VAR_DIMS];
    for (int i = 0; i < start.size(); i++) {
        mystart[i] = start[i];
        mycount[i] = count[i];
    }
    return (NetCDFCollection::Read(mystart, mycount, data, fd));
}

int NetCDFCollection::Read(vector<size_t> start, vector<size_t> count, int *data, int fd)
{
    VAssert(start.size() == count.size());

    size_t mystart[NC_MAX_VAR_DIMS];
    size_t mycount[NC_MAX_VAR_DIMS];
    for (int i = 0; i < start.size(); i++) {
        mystart[i] = start[i];
        mycount[i] = count[i];
    }
    return (NetCDFCollection::Read(mystart, mycount, data, fd));
}


template<typename T> int NetCDFCollection::_read_template(T *data, int fd)
{
    std::map<int, fileHandle>::iterator itr;
    if ((itr = _ovr_table.find(fd)) == _ovr_table.end()) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return (-1);
    }
    fileHandle &fh = itr->second;

    const TimeVaryingVar &var = fh._tvvars;
    vector<size_t>        dims = var.GetSpatialDims();
    vector<string>        dimnames = var.GetSpatialDimNames();

    //
    // Handle different dimenion cases
    //
    if (dims.size() > 3) {
        SetErrMsg("Only 0D, 1D, 2D and 3D variables supported");
        return (-1);
    }

    size_t start[3] = {0, 0, 0};
    size_t count[3];
    if (dims.size() == 0) {
        count[0] = 1;
        return (NetCDFCollection::Read(start, count, data, fd));
    } else if (dims.size() == 1) {
        size_t nx = dims[dims.size() - 1];
        count[0] = nx;
    } else if (dims.size() == 2) {
        size_t nx = dims[dims.size() - 1];
        size_t ny = dims[dims.size() - 2];
        count[0] = ny;
        count[1] = nx;
    } else if (dims.size() == 3) {
        size_t nx = dims[dims.size() - 1];
        size_t ny = dims[dims.size() - 2];
        size_t nz = dims[dims.size() - 3];
        count[0] = nz;
        count[1] = ny;
        count[2] = nx;
    }

    return (NetCDFCollection::Read(start, count, data, fd));
}

int NetCDFCollection::Read(char *data, int fd) { return (_read_template(data, fd)); }

int NetCDFCollection::Read(int *data, int fd) { return (_read_template(data, fd)); }

int NetCDFCollection::Read(float *data, int fd) { return (_read_template(data, fd)); }

int NetCDFCollection::Read(double *data, int fd) { return (_read_template(data, fd)); }

int NetCDFCollection::Close(int fd)
{
    std::map<int, fileHandle>::iterator itr;
    if ((itr = _ovr_table.find(fd)) == _ovr_table.end()) {
        SetErrMsg("Invalid file descriptor : %d", fd);
        return (-1);
    }
    fileHandle &fh = itr->second;

    if (!fh._ncdfptr) return (0);

    int rc = fh._ncdfptr->Close(fh._fd);
    if (fh._slicebuf) delete[] fh._slicebuf;
    if (fh._linebuf) delete[] fh._linebuf;

    _ovr_table.erase(itr);
    return (rc);
}

namespace VAPoR {
std::ostream &operator<<(std::ostream &o, const NetCDFCollection &ncdfc)
{
    o << "NetCDFCollection" << endl;
    o << " _times : ";
    for (int i = 0; i < ncdfc._times.size(); i++) { o << ncdfc._times[i] << " "; }
    o << endl;
    o << " _missingValAttName : " << ncdfc._missingValAttName << endl;

    o << " _variableList : " << endl;
    map<string, NetCDFCollection::TimeVaryingVar>::const_iterator itr;
    for (itr = ncdfc._variableList.begin(); itr != ncdfc._variableList.end(); ++itr) {
        o << itr->second;
        o << endl;
    }

    return (o);
}
};    // namespace VAPoR

NetCDFCollection::TimeVaryingVar::TimeVaryingVar()
{
    _files.clear();
    _tvmaps.clear();
    _spatial_dims.clear();
    _spatial_dim_names.clear();
    _name.clear();
    _time_name.clear();
    _time_varying = false;
}

int NetCDFCollection::TimeVaryingVar::Insert(const NetCDFSimple *netcdf, const NetCDFSimple::Variable &variable, string file, const vector<string> &time_dimnames,
                                             const map<string, vector<double>> &timesmap, int file_org)
{
    bool first = (_tvmaps.size() == 0);    // first insertion?

    vector<string> space_dim_names = variable.GetDimNames();
    vector<size_t> space_dims;
    for (int i = 0; i < space_dim_names.size(); i++) { space_dims.push_back(netcdf->DimLen(space_dim_names[i])); }
    string time_name;

    //
    // Check if variable is time varying. I.e. if its slowest varying
    // dimension name matches a dimension name specified in time_dimnames
    //
    bool   time_varying = false;
    string key;    // hash key for timesmap

    if (variable.GetDimNames().size()) {
        string s = variable.GetDimNames()[0];

        // Handle ITVV case
        //
        if (time_dimnames.size() == 1 && time_dimnames[0] == derivedTimeDimName) {
            time_varying = true;
            time_name = derivedTimeDimName;
        } else if (find(time_dimnames.begin(), time_dimnames.end(), s) != time_dimnames.end()) {
            time_varying = true;
            time_name = s;
            space_dims.erase(space_dims.begin());
            space_dim_names.erase(space_dim_names.begin());
        }
    }

    if (!time_varying) {
        key = "constant";
    } else if (file_org == 1 || file_org == 2) {
        key = file + variable.GetName();
    } else {
        key = file + variable.GetDimNames()[0];
    }

    if (first) {
        _spatial_dims = space_dims;
        _spatial_dim_names = space_dim_names;
        _time_varying = time_varying;
        _name = variable.GetName();
        _time_name = time_name;
        _variable = variable;
    } else {
        //
        // If this isn't the first variable to be inserted the new variable
        // must match the existing ones
        //
        if (!((variable.GetDimNames() == _variable.GetDimNames()) && variable.GetXType() == _variable.GetXType())) {
            SetErrMsg("Multiple definitions of variable \"%s\"", variable.GetName().c_str());
            return (-1);
        }
    }
    _files.push_back(file);

    map<string, vector<double>>::const_iterator itr;
    itr = timesmap.find(key);
    if (itr == timesmap.end()) {
        SetErrMsg("Time coordinates not available for variable");
        return (-1);
    }
    const vector<double> &timesref = itr->second;

    size_t local_ts = 0;
    for (int i = 0; i < timesref.size(); i++) {
        tvmap_t tvmap;
        tvmap._fileidx = _files.size() - 1;
        tvmap._time = timesref[i];
        tvmap._local_ts = local_ts;
        _tvmaps.push_back(tvmap);
        local_ts++;
    }

    return (0);
}

int NetCDFCollection::TimeVaryingVar::GetTime(size_t ts, double &time) const
{
    if (ts >= _tvmaps.size()) return (-1);

    time = _tvmaps[ts]._time;

    return (0);
}

vector<double> NetCDFCollection::TimeVaryingVar::GetTimes() const
{
    vector<double> times;

    for (int i = 0; i < _tvmaps.size(); i++) times.push_back(_tvmaps[i]._time);

    return (times);
}

int NetCDFCollection::TimeVaryingVar::GetTimeStep(double time, size_t &ts) const
{
    if (!_time_varying) {
        ts = 0;
        return (0);
    }

    for (size_t i = 0; i < _tvmaps.size(); i++) {
        if (_tvmaps[i]._time == time) {
            ts = i;
            return (0);
        }
    }
    SetErrMsg("Invalid time %f", time);
    return (-1);
}

size_t NetCDFCollection::TimeVaryingVar::GetLocalTimeStep(size_t ts) const
{
    if (ts >= _tvmaps.size()) return (0);

    return (_tvmaps[ts]._local_ts);
}

int NetCDFCollection::TimeVaryingVar::GetFile(size_t ts, string &file) const
{
    if (ts >= _tvmaps.size()) return (-1);

    int fileidx = _tvmaps[ts]._fileidx;
    file = _files[fileidx];
    return (0);
}

bool NetCDFCollection::TimeVaryingVar::GetMissingValue(string attname, double &mv) const
{
    mv = 0.0;

    if (!attname.length()) return (false);

    vector<double> vec;
    _variable.GetAtt(attname, vec);
    if (!vec.size()) return (false);

    mv = vec[0];
    return (true);
}

void NetCDFCollection::TimeVaryingVar::Sort()
{
    //
    // Sort variable by time
    //

    auto lambda = [](const NetCDFCollection::TimeVaryingVar::tvmap_t &s1, const NetCDFCollection::TimeVaryingVar::tvmap_t &s2) -> bool { return (s1._time < s2._time); };

    std::sort(_tvmaps.begin(), _tvmaps.end(), lambda);
}

NetCDFCollection::fileHandle::fileHandle()
{
    _ncdfptr = NULL;
    _fd = -1;
    _local_ts = 0;
    _slice = 0;
    _slicebuf = NULL;
    _slicebufsz = 0;
    _linebuf = NULL;
    _linebufsz = 0;
    _has_missing = false;
    _missing_value = 0.0;
}

namespace VAPoR {
std::ostream &operator<<(std::ostream &o, const NetCDFCollection::TimeVaryingVar &var)
{
    o << " TimeVaryingVar" << endl;
    o << " Variable : " << var._name << endl;
    o << "  Files : " << endl;
    for (int i = 0; i < var._files.size(); i++) { o << "   " << var._files[i] << endl; }
    o << "  Dims : ";
    for (int i = 0; i < var._spatial_dims.size(); i++) { o << var._spatial_dims[i] << " "; }
    o << endl;
    o << "  Dim Names : ";
    for (int i = 0; i < var._spatial_dim_names.size(); i++) { o << var._spatial_dim_names[i] << " "; }
    o << endl;

    o << "  Time Varying : " << var._time_varying << endl;

    o << "  Time Varying Map : " << endl;
    for (int i = 0; i < var._tvmaps.size(); i++) {
        o << "   _fileidx : " << var._tvmaps[i]._fileidx << endl;
        o << "   _time : " << var._tvmaps[i]._time << endl;
        o << "   _local_ts : " << var._tvmaps[i]._local_ts << endl;
        o << endl;
    }

    return (o);
}
};    // namespace VAPoR
