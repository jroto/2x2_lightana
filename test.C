#include <highfive/H5File.hpp>
#include <iostream>
#include <exception>


using HighFive::File;

void print_datatype_class(const HighFive::DataType& dtype)
{
    std::cout << "Datatype class: ";
    switch (dtype.getClass()) {
        case HighFive::DataTypeClass::Integer:
            std::cout << "Integer";
            break;
        case HighFive::DataTypeClass::Float:
            std::cout << "Float";
            break;
        case HighFive::DataTypeClass::String:
            std::cout << "String";
            break;
        case HighFive::DataTypeClass::Compound:
            std::cout << "Compound";
            break;
        default:
            std::cout << "Other";
            break;
    }
    std::cout << std::endl;
}

void print_dataset_info(HighFive::DataSet& ds)
{
    // Dataspace information
    auto space = ds.getSpace();

    std::cout << "Rank: "
              << space.getNumberDimensions()
              << std::endl;

    std::cout << "Dimensions: ";
    for (auto d : space.getDimensions())
        std::cout << d << " ";
    std::cout << std::endl;

    std::cout << "Number of elements: "
              << space.getElementCount()
              << std::endl;

    // Datatype information
    auto dtype = ds.getDataType();

    std::cout << "Datatype size: "
              << dtype.getSize()
              << " bytes"
              << std::endl;

    print_datatype_class(dtype);
}
void test() {

    const std::string filename = "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5";
    const std::string dataset_path = "/light/wvfm/data";

    try {
        File file(filename, File::ReadOnly);
        auto ds = file.getDataSet(dataset_path);
        print_dataset_info(ds);
    } catch (const HighFive::Exception& e) {
        std::cerr << "HighFive error while reading '" << dataset_path
                  << "' from file '" << filename << "':\n"
                  << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
    }

}
