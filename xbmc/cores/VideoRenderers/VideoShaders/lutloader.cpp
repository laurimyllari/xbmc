#include "config.h"
#include "lutloader.h"
#include "lcms2.h"
#include "lcms2_plugin.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_LIBLCMS2)

int loadLUT(unsigned flags,
    float **CLUT,
    int *CLUTsize,
    float **outluts,
    int *outlutsize)
{
    cmsHPROFILE hProfile;
    cmsPipeline *pipeline;
    cmsStage *inputstage, *clutstage, *outputstage;
    cmsToneCurve **inputcurves, **outputcurves;
    int lutsamples;

    // FIXME - device link filename based on colorspace in flags
    hProfile = cmsOpenProfileFromFile("rec709.icc", "r");
    if (!hProfile) {
        printf("device link not found\n");
        return 1;
    }
    if (cmsGetDeviceClass(hProfile) != cmsSigLinkClass) {
        printf("expected device link profile\n");
        return 1;
        // TODO: create a source profile, link together for device link, convert to profile, use that?
    }

    if (!cmsIsTag(hProfile, cmsSigAToB0Tag)) {
        printf("expected to find AToB0 tag\n");
        return 1;
    }

    pipeline = (cmsPipeline*)cmsReadTag(hProfile, cmsSigAToB0Tag);

    if (cmsPipelineInputChannels(pipeline) != 3 || cmsPipelineOutputChannels(pipeline) != 3) {
        printf("expected 3 channels\n");
        return 1;
    }

    if (!cmsPipelineCheckAndRetreiveStages(
            pipeline,
            3,
            cmsSigCurveSetElemType, cmsSigCLutElemType, cmsSigCurveSetElemType,
            &inputstage, &clutstage, &outputstage)) {
        printf("expected <table, clut, table> in AToB0\n");
        return 1;
    }

    // to use YCbCr source encoding with collink, curves must be disabled
    inputcurves = ((_cmsStageToneCurvesData *)cmsStageData(inputstage))->TheCurves;

    for (int c = 0; c < 3; c++ )
        if (!cmsIsToneCurveLinear(inputcurves[c])) {
                printf("expected linear input table\n");
                return 1;
        }

    // It is possible that output curve has been added with applycal. This
    // gives two options for including device calibration: -a to collink
    // (calibration is applied to the 3DLUT?) or applycal. Would the latter
    // give better precision?
    outputcurves = ((_cmsStageToneCurvesData *)cmsStageData(outputstage))->TheCurves;
    if (cmsIsToneCurveLinear(outputcurves[0])
            && cmsIsToneCurveLinear(outputcurves[1])
            && cmsIsToneCurveLinear(outputcurves[2])) {
        printf("output curves seem linear and could be ignored\n");
        *outlutsize = 16;
    } else {
        // Use the same size as lcms2 uses for its estimated representation
        *outlutsize = cmsGetToneCurveEstimatedTableEntries(outputcurves[0]);
        printf("using output table size %d\n", *outlutsize);
    }

#ifdef DEBUG
    for (int c = 0; c < 3; c++ ) {
        printf("outputcurves[%d]:\n" \
                "\tisMultisegment: %s\n" \
                "\tisLinear: %s\n" \
                "\tisMonotonic: %s\n" \
                "\tisDescending: %s\n" \
                "\tgammaEstimate: %f\n" \
                "\testimatedTableEntries: %d\n",
                c,
                cmsIsToneCurveMultisegment(outputcurves[c]) ? "true" : "false",
                cmsIsToneCurveLinear(outputcurves[c]) ? "true" : "false",
                cmsIsToneCurveMonotonic(outputcurves[c]) ? "true" : "false",
                cmsIsToneCurveDescending(outputcurves[c]) ? "true" : "false",
                cmsEstimateGamma(outputcurves[c], 0.01),
                cmsGetToneCurveEstimatedTableEntries(outputcurves[c]));
    }
#endif

    *outluts = (float*)malloc(*outlutsize * 3 * sizeof(float));
    for (int c = 0; c < 3; c++ ) {
        for (int x = 0; x < *outlutsize; x++) {
            (*outluts)[3*c + x] = cmsEvalToneCurveFloat(
                    outputcurves[c],
                    (float)x / (*outlutsize-1));
        }
    }

#ifdef DEBUG
    for (int x = 0; x < *outlutsize; x++) {
        printf("%2d: ", x);
        for (int c = 0; c < 3; c++ )
            printf(" %0.4f", *outluts[3*c + x]);
        printf("\n");
    }
#endif

    _cmsStageCLutData *clutdata = ((_cmsStageCLutData *)cmsStageData(clutstage));
    lutsamples = clutdata->nEntries;
    *CLUTsize = round(pow(lutsamples / 3, 1.0/3));

    printf("lut size %dx%dx%d, %s\n",
            *CLUTsize, *CLUTsize, *CLUTsize, clutdata->HasFloatValues ? "float" : "uint16");

    if ((*CLUTsize)*(*CLUTsize)*(*CLUTsize)*3 != lutsamples) {
        printf("calculated LUT dimensions don't match sample count\n");
        return 1;
    }

    *CLUT = (float*)malloc(lutsamples * sizeof(float));

    for (int i = 0; i < lutsamples; i++) {
        if (clutdata->HasFloatValues)
            (*CLUT)[i] = clutdata->Tab.TFloat[i];
        else
            (*CLUT)[i] = clutdata->Tab.T[i] / 65535.0F;
    }

    cmsCloseProfile(hProfile);

    return 0;
}
#endif
