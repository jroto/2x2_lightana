#pragma once
//
// SubRunReader.hpp
//
// Internal, low-level reader for a single NDLAr light-system subrun file.
// Opens `/light/events/data` and `/light/wvfm/data`, and reads them one
// row at a time via HDF5 hyperslab selection (never the whole dataset at
// once). Row `i` of one dataset is assumed to correspond to row `i` of
// the other (validated by a dataset-length check at open time).
//
// This class is not meant to be used directly by macros - use Run, which
// wraps one or more SubRunReader instances to iterate a whole run
// transparently. It's exposed here (not in an anonymous namespace) in
// case a macro needs single-file access without a full Run.
//
// Nested-array compound types (samples[8][64][600], clipped[8][64],
// sn[8], utime_ms[8], tai_ns[8], wvfm_valid[8][64]) are not something
// HighFive's automatic type mapping handles, so this class builds the
// matching HDF5 compound `hid_t` by hand with the raw C API
// (H5Tarray_create2 / H5Tcreate(H5T_COMPOUND, ...) / H5Tinsert) and reads
// via H5Dread, using HighFive::DataSet::getId() to get the raw dataset
// handle for interop.
//

#include "Event.hpp"

#include <highfive/H5File.hpp>
#include <hdf5.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace ndlar_light {

namespace detail {

// Raw layout matching `/light/events/data`:
//   id         : uint64
//   event      : int32
//   sn         : int32[8]
//   utime_ms   : uint64[8]
//   tai_ns     : uint64[8]
//   wvfm_valid : uint8[8][64]
//   trig_type  : uint8
struct EventsRecord {
    uint64_t id;
    int32_t event;
    int32_t sn[kNumADCs];
    uint64_t utime_ms[kNumADCs];
    uint64_t tai_ns[kNumADCs];
    uint8_t wvfm_valid[kNumADCs][kNumChannels];
    uint8_t trig_type;
};

// Raw layout matching `/light/wvfm/data`:
//   samples : int16[8][64][600]
//   clipped : enum(int8) [8][64]
struct WvfmRecord {
    int16_t samples[kNumADCs][kNumChannels][kNumSamples];
    int8_t clipped[kNumADCs][kNumChannels];
};

/// Builds the raw HDF5 compound datatype matching EventsRecord's layout.
/// Caller must close the returned hid_t with H5Tclose().
inline hid_t MakeEventsCompoundType()
{
    hsize_t sn_dims[1] = {kNumADCs};
    hid_t sn_tid = H5Tarray_create2(H5T_NATIVE_INT32, 1, sn_dims);
    if (sn_tid < 0) throw std::runtime_error("SubRunReader: failed to create sn array type");

    hsize_t time_dims[1] = {kNumADCs};
    hid_t utime_tid = H5Tarray_create2(H5T_NATIVE_UINT64, 1, time_dims);
    if (utime_tid < 0) { H5Tclose(sn_tid); throw std::runtime_error("SubRunReader: failed to create utime_ms array type"); }

    hid_t tai_tid = H5Tarray_create2(H5T_NATIVE_UINT64, 1, time_dims);
    if (tai_tid < 0) { H5Tclose(sn_tid); H5Tclose(utime_tid); throw std::runtime_error("SubRunReader: failed to create tai_ns array type"); }

    hsize_t valid_dims[2] = {kNumADCs, kNumChannels};
    hid_t valid_tid = H5Tarray_create2(H5T_NATIVE_UINT8, 2, valid_dims);
    if (valid_tid < 0) {
        H5Tclose(sn_tid); H5Tclose(utime_tid); H5Tclose(tai_tid);
        throw std::runtime_error("SubRunReader: failed to create wvfm_valid array type");
    }

    hid_t compound_tid = H5Tcreate(H5T_COMPOUND, sizeof(EventsRecord));
    H5Tinsert(compound_tid, "id", HOFFSET(EventsRecord, id), H5T_NATIVE_UINT64);
    H5Tinsert(compound_tid, "event", HOFFSET(EventsRecord, event), H5T_NATIVE_INT32);
    H5Tinsert(compound_tid, "sn", HOFFSET(EventsRecord, sn), sn_tid);
    H5Tinsert(compound_tid, "utime_ms", HOFFSET(EventsRecord, utime_ms), utime_tid);
    H5Tinsert(compound_tid, "tai_ns", HOFFSET(EventsRecord, tai_ns), tai_tid);
    H5Tinsert(compound_tid, "wvfm_valid", HOFFSET(EventsRecord, wvfm_valid), valid_tid);
    H5Tinsert(compound_tid, "trig_type", HOFFSET(EventsRecord, trig_type), H5T_NATIVE_UINT8);

    H5Tclose(sn_tid);
    H5Tclose(utime_tid);
    H5Tclose(tai_tid);
    H5Tclose(valid_tid);

    return compound_tid;
}

/// Builds the raw HDF5 compound datatype matching WvfmRecord's layout.
/// Caller must close the returned hid_t with H5Tclose().
inline hid_t MakeWvfmCompoundType()
{
    hsize_t samples_dims[3] = {kNumADCs, kNumChannels, kNumSamples};
    hid_t samples_tid = H5Tarray_create2(H5T_NATIVE_INT16, 3, samples_dims);
    if (samples_tid < 0) throw std::runtime_error("SubRunReader: failed to create samples array type");

    hsize_t clipped_dims[2] = {kNumADCs, kNumChannels};
    hid_t clipped_tid = H5Tarray_create2(H5T_NATIVE_INT8, 2, clipped_dims);
    if (clipped_tid < 0) { H5Tclose(samples_tid); throw std::runtime_error("SubRunReader: failed to create clipped array type"); }

    hid_t compound_tid = H5Tcreate(H5T_COMPOUND, sizeof(WvfmRecord));
    H5Tinsert(compound_tid, "samples", HOFFSET(WvfmRecord, samples), samples_tid);
    H5Tinsert(compound_tid, "clipped", HOFFSET(WvfmRecord, clipped), clipped_tid);

    H5Tclose(samples_tid);
    H5Tclose(clipped_tid);

    return compound_tid;
}

} // namespace detail

/// Low-level reader for one subrun file. Opens the two datasets, builds
/// the raw compound types needed to read them, and reads one row at a
/// time into a caller-provided Event (no per-event heap allocation).
class SubRunReader {
public:
    /// Opens `filename` and prepares to read `/light/events/data` and
    /// `/light/wvfm/data`. Throws std::runtime_error (with the file path
    /// in the message) if the file can't be opened, the datasets are
    /// missing, or their lengths don't match.
    explicit SubRunReader(const std::string& filename)
        : fFilename(filename)
        , fFile(filename, HighFive::File::ReadOnly)
    {
        try {
            fEventsDataset = std::make_unique<HighFive::DataSet>(fFile.getDataSet(kEventsPath));
            fWvfmDataset = std::make_unique<HighFive::DataSet>(fFile.getDataSet(kWvfmPath));
        } catch (const HighFive::Exception& e) {
            throw std::runtime_error(
                "SubRunReader: failed to open datasets in '" + filename + "': " + e.what());
        }

        const size_t n_events = fEventsDataset->getSpace().getDimensions()[0];
        const size_t n_wvfm = fWvfmDataset->getSpace().getDimensions()[0];
        if (n_events != n_wvfm) {
            throw std::runtime_error(
                "SubRunReader: '" + filename + "' has mismatched lengths for " +
                std::string(kEventsPath) + " (" + std::to_string(n_events) + ") and " +
                std::string(kWvfmPath) + " (" + std::to_string(n_wvfm) +
                "); expected 1:1 row correspondence. NOTE: this class assumes simple "
                "identity-index correspondence between the two datasets and does not "
                "cross-check the HDF5 ref/ref_region datasets - see Instructions.md.");
        }
        fNumEvents = n_events;

        fEventsCompoundType = detail::MakeEventsCompoundType();
        fWvfmCompoundType = detail::MakeWvfmCompoundType();

        fEventsFileSpace = H5Dget_space(fEventsDataset->getId());
        fWvfmFileSpace = H5Dget_space(fWvfmDataset->getId());

        hsize_t mem_dims[1] = {1};
        fMemSpace = H5Screate_simple(1, mem_dims, nullptr);
    }

    ~SubRunReader()
    {
        if (fMemSpace >= 0) H5Sclose(fMemSpace);
        if (fWvfmFileSpace >= 0) H5Sclose(fWvfmFileSpace);
        if (fEventsFileSpace >= 0) H5Sclose(fEventsFileSpace);
        if (fWvfmCompoundType >= 0) H5Tclose(fWvfmCompoundType);
        if (fEventsCompoundType >= 0) H5Tclose(fEventsCompoundType);
    }

    // Not copyable (owns raw HDF5 handles); movable would be easy to add
    // later if needed, but Run only needs one instance alive at a time.
    SubRunReader(const SubRunReader&) = delete;
    SubRunReader& operator=(const SubRunReader&) = delete;

    /// Number of events (rows) in this subrun file.
    size_t NumEvents() const { return fNumEvents; }

    /// Path of the file this reader was opened on.
    const std::string& Filename() const { return fFilename; }

    /// Reads row `row` (0-based) from both datasets and fills `event` in
    /// place. Throws std::runtime_error if `row` is out of range or the
    /// underlying H5Dread call fails.
    void ReadRow(size_t row, Event& event)
    {
        if (row >= fNumEvents) {
            throw std::runtime_error(
                "SubRunReader::ReadRow: row " + std::to_string(row) + " out of range (" +
                std::to_string(fNumEvents) + " events) in '" + fFilename + "'");
        }

        detail::EventsRecord events_record{};
        detail::WvfmRecord wvfm_record{};

        hsize_t start[1] = {row};
        hsize_t count[1] = {1};

        H5Sselect_hyperslab(fEventsFileSpace, H5S_SELECT_SET, start, nullptr, count, nullptr);
        herr_t status = H5Dread(fEventsDataset->getId(), fEventsCompoundType, fMemSpace,
                                 fEventsFileSpace, H5P_DEFAULT, &events_record);
        if (status < 0) {
            throw std::runtime_error(
                "SubRunReader::ReadRow: H5Dread failed on " + std::string(kEventsPath) +
                " row " + std::to_string(row) + " in '" + fFilename + "'");
        }

        H5Sselect_hyperslab(fWvfmFileSpace, H5S_SELECT_SET, start, nullptr, count, nullptr);
        status = H5Dread(fWvfmDataset->getId(), fWvfmCompoundType, fMemSpace,
                          fWvfmFileSpace, H5P_DEFAULT, &wvfm_record);
        if (status < 0) {
            throw std::runtime_error(
                "SubRunReader::ReadRow: H5Dread failed on " + std::string(kWvfmPath) +
                " row " + std::to_string(row) + " in '" + fFilename + "'");
        }

        FillEvent(events_record, wvfm_record, event);
    }

private:
    static constexpr const char* kEventsPath = "/light/events/data";
    static constexpr const char* kWvfmPath = "/light/wvfm/data";

    static void FillEvent(const detail::EventsRecord& er, const detail::WvfmRecord& wr, Event& event)
    {
        EventMetadata& meta = event.Meta();
        meta.SetId(er.id);
        meta.SetEventNumber(er.event);
        meta.SetTriggerType(er.trig_type);

        for (int adc = 0; adc < kNumADCs; ++adc) {
            meta.SetSerialNumber(adc, er.sn[adc]);
            meta.SetUTimeMs(adc, er.utime_ms[adc]);
            meta.SetTaiNs(adc, er.tai_ns[adc]);

            for (int ch = 0; ch < kNumChannels; ++ch) {
                meta.SetValid(adc, ch, er.wvfm_valid[adc][ch] != 0);

                Waveform& wf = event.MutableWaveform(adc, ch);
                wf.SetADC(adc);
                wf.SetChannel(ch);
                wf.SetClipped(wr.clipped[adc][ch] != 0);

                int16_t* dst = wf.MutableData();
                const int16_t* src = wr.samples[adc][ch];
                for (size_t s = 0; s < kNumSamples; ++s) dst[s] = src[s];
            }
        }
    }

    std::string fFilename;
    HighFive::File fFile;
    std::unique_ptr<HighFive::DataSet> fEventsDataset;
    std::unique_ptr<HighFive::DataSet> fWvfmDataset;

    size_t fNumEvents = 0;

    hid_t fEventsCompoundType = -1;
    hid_t fWvfmCompoundType = -1;
    hid_t fEventsFileSpace = -1;
    hid_t fWvfmFileSpace = -1;
    hid_t fMemSpace = -1;
};

} // namespace ndlar_light
