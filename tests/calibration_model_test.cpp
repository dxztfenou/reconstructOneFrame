#include "calibration_model/CalibrationModel.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace reconstruct_one_frame;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main()
{
    CalibrationModel model;
    Status status = loadCalibrationResultJson("tests/data/phase2_valid_calibResult.json", model);
    require(status.ok(), "expected valid calibration to load");
    require(model.leftIntrinsics.size() == 9, "expected left intrinsics");
    require(model.translation.size() == 3, "expected translation");

    status = validateCalibrationImageSize(model, 424, 400);
    require(status.ok(), "expected calibration image size to match");

    status = loadCalibrationResultJson("tests/data/does_not_exist_calibResult.json", model);
    require(status.code == StatusCode::CalibrationMissing, "expected CalibrationMissing");

    const auto invalidPath = std::filesystem::temp_directory_path() / "rof_invalid_calib.json";
    {
        std::ofstream out(invalidPath);
        out << "not json";
    }
    status = loadCalibrationResultJson(invalidPath.string(), model);
    std::filesystem::remove(invalidPath);
    require(status.code == StatusCode::CalibrationFieldMissing, "expected CalibrationFieldMissing for unrecognized calibration text");

    const auto missingFieldPath = std::filesystem::temp_directory_path() / "rof_missing_calib_field.json";
    {
        std::ofstream out(missingFieldPath);
        out << "{\"image_size\":[424,400]}";
    }
    status = loadCalibrationResultJson(missingFieldPath.string(), model);
    std::filesystem::remove(missingFieldPath);
    require(status.code == StatusCode::CalibrationFieldMissing, "expected CalibrationFieldMissing");

    status = loadCalibrationResultJson("tests/data/phase2_bad_calib_matrix.json", model);
    require(status.code == StatusCode::CalibrationMatrixShapeInvalid, "expected CalibrationMatrixShapeInvalid");

    status = loadCalibrationResultJson("tests/data/phase2_mismatch_calibResult.json", model);
    require(status.ok(), "expected mismatch calibration to parse");
    status = validateCalibrationImageSize(model, 424, 400);
    require(status.code == StatusCode::CalibrationImageSizeMismatch, "expected CalibrationImageSizeMismatch");

    status = loadCalibrationResultJson("tests/data/phase5_opencv_matrix_calibResult.json", model);
    require(status.ok(), "expected OpenCV matrix calibration to parse");
    require(model.leftIntrinsics.size() == 9, "expected KK_L to map to left intrinsics");
    require(model.leftDistortion.size() == 5, "expected Dist_L to map to left distortion");
    require(model.rectificationLeft.size() == 9, "expected R_L matrix data");
    require(model.projectionLeft.size() == 12, "expected P_L matrix data");
    require(model.qMatrix.size() == 16, "expected OpenCV Q matrix data");

    const auto yamlPath = std::filesystem::temp_directory_path() / "rof_calibParams.yml";
    {
        std::ofstream out(yamlPath);
        out << "%YAML:1.0\n"
            << "KK_L: !!opencv-matrix\n   rows: 3\n   cols: 3\n   dt: d\n   data: [1,0,0,0,1,0,0,0,1]\n"
            << "Dist_L: !!opencv-matrix\n   rows: 5\n   cols: 1\n   dt: d\n   data: [0,0,0,0,0]\n"
            << "KK_R: !!opencv-matrix\n   rows: 3\n   cols: 3\n   dt: d\n   data: [1,0,0,0,1,0,0,0,1]\n"
            << "Dist_R: !!opencv-matrix\n   rows: 5\n   cols: 1\n   dt: d\n   data: [0,0,0,0,0]\n"
            << "R: !!opencv-matrix\n   rows: 3\n   cols: 3\n   dt: d\n   data: [1,0,0,0,1,0,0,0,1]\n"
            << "T: !!opencv-matrix\n   rows: 3\n   cols: 1\n   dt: d\n   data: [1,0,0]\n"
            << "R_L: !!opencv-matrix\n   rows: 3\n   cols: 3\n   dt: d\n   data: [1,0,0,0,1,0,0,0,1]\n"
            << "R_R: !!opencv-matrix\n   rows: 3\n   cols: 3\n   dt: d\n   data: [1,0,0,0,1,0,0,0,1]\n"
            << "P_L: !!opencv-matrix\n   rows: 3\n   cols: 4\n   dt: d\n   data: [1,0,0,0,0,1,0,0,0,0,1,0]\n"
            << "P_R: !!opencv-matrix\n   rows: 3\n   cols: 4\n   dt: d\n   data: [1,0,0,0,0,1,0,0,0,0,1,0]\n"
            << "Q: !!opencv-matrix\n   rows: 4\n   cols: 4\n   dt: d\n   data: [1,0,0,0,0,1,0,0,0,0,0,1,0,0,1,0]\n"
            << "image_size: [424, 400]\n";
    }
    status = loadCalibrationResultJson(yamlPath.string(), model);
    std::filesystem::remove(yamlPath);
    require(status.ok(), "expected OpenCV YAML calibParams to parse");
    require(model.qMatrix.size() == 16, "expected YAML Q matrix data");
    require(model.projectionRight.size() == 12, "expected YAML P_R matrix data");

    return EXIT_SUCCESS;
}
