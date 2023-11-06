/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __SAMPLE_RATE_CONVERTER_FILTER__
#define __SAMPLE_RATE_CONVERTER_FILTER__

#include <dsp/filtering_functions.h>

/**
 * @brief Get the pointer to the filter coefficients
 *
 * @param[in] filter_type Selected filter type
 * @param[in] conversion_ratio Ratio for the conversion the filter will be used for
 * @param[out] filter_ptr Pointer to the filter coefficients
 * @param[out] filter_size Number of filter coefficients
 *
 * @retval 0 On success
 * @retval -EINVAL Bit depth not supported
 */
int sample_rate_converter_filter_get(enum sample_rate_converter_filter filter_type,
				     uint8_t conversion_ratio, void **filter_ptr,
				     size_t *filter_size);

#endif /* __SAMPLE_RATE_CONVERTER_FILTER__ */
