#include <highfive/H5File.hpp>
#include <hdf5.h>   // raw HDF5 C API, needed for the nested-array compound type

#include <iostream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

using HighFive::File;
// h5dump -H /pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5 /light/wvfm/data

// Matches:
//   H5T_ARRAY {[8][64][600]} H5T_STD_I16LE "samples"
//   H5T_ARRAY {[8][64]}      H5T_ENUM(int8) "clipped"
struct WvfmRecord {
    int16_t samples[8][64][600];
    int8_t  clipped[8][64];   // 0 = FALSE, 1 = TRUE
};

// Builds the raw HDF5 compound datatype matching WvfmRecord's layout.
// Caller is responsible for closing the returned hid_t with H5Tclose().
static hid_t make_wvfm_compound_type()
{
    hsize_t samples_dims[3] = {8, 64, 600};
    hid_t samples_tid = H5Tarray_create2(H5T_NATIVE_INT16, 3, samples_dims);
    if (samples_tid < 0) throw std::runtime_error("Failed to create samples array type");

    hsize_t clipped_dims[2] = {8, 64};
    hid_t clipped_tid = H5Tarray_create2(H5T_NATIVE_INT8, 2, clipped_dims);
    if (clipped_tid < 0) { H5Tclose(samples_tid); throw std::runtime_error("Failed to create clipped array type"); }

    hid_t compound_tid = H5Tcreate(H5T_COMPOUND, sizeof(WvfmRecord));
    H5Tinsert(compound_tid, "samples", HOFFSET(WvfmRecord, samples), samples_tid);
    H5Tinsert(compound_tid, "clipped", HOFFSET(WvfmRecord, clipped), clipped_tid);

    // These can be closed now; H5Tinsert copies the type info into compound_tid.
    H5Tclose(samples_tid);
    H5Tclose(clipped_tid);

    return compound_tid;
}

// Loops over the dataset one event ("record") at a time and prints a
// summary. Change max_events (or drop the cap) to print everything.
void loop_and_print_wvfm(HighFive::DataSet& ds, size_t max_events = 5)
{
    auto space = ds.getSpace();
    const size_t n_events = space.getDimensions()[0];
    const size_t n_print  = std::min(n_events, max_events);

    std::cout << "Dataset has " << n_events << " events; printing "
              << n_print << ".\n";

    hid_t compound_tid = make_wvfm_compound_type();

    // Raw handles: HighFive::Object exposes getId() for interop with the C API.
    hid_t dataset_id   = ds.getId();
    hid_t file_space_id = H5Dget_space(dataset_id);

    hsize_t mem_dims[1] = {1};
    hid_t mem_space_id = H5Screate_simple(1, mem_dims, nullptr);

    WvfmRecord record;

    for (size_t i = 0; i < n_print; ++i) {
        hsize_t start[1] = {i};
        hsize_t count[1] = {1};
        H5Sselect_hyperslab(file_space_id, H5S_SELECT_SET, start, nullptr, count, nullptr);

        herr_t status = H5Dread(dataset_id, compound_tid, mem_space_id,
                                 file_space_id, H5P_DEFAULT, &record);
        if (status < 0) {
            std::cerr << "H5Dread failed at event " << i << "\n";
            break;
        }

        std::cout << "Event " << i << ":\n";
        for (int adc = 0; adc < 8; ++adc) {
            for (int ch = 0; ch < 64; ++ch) {
                // Skip channels that are entirely padding in a lot of files;
                // remove this check to print every ADC/channel.
                std::cout << "  ADC " << adc << " CH " << ch
                          << " clipped=" << static_cast<int>(record.clipped[adc][ch])
                          << " samples[0:5]=";
                for (int s = 0; s < 5; ++s)
                    std::cout << record.samples[adc][ch][s] << " ";
                std::cout << "...\n";
            }
        }
    }

    H5Sclose(mem_space_id);
    H5Sclose(file_space_id);
    H5Tclose(compound_tid);
}

void test2()
{
    const std::string filename =
        "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/"
        "mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5";
    const std::string dataset_path = "/light/wvfm/data";

    try {
        File file(filename, File::ReadOnly);
        auto ds = file.getDataSet(dataset_path);
        loop_and_print_wvfm(ds, /*max_events=*/5);
    } catch (const HighFive::Exception& e) {
        std::cerr << "HighFive error while reading '" << dataset_path
                   << "' from file '" << filename << "':\n"
                   << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
    }
}