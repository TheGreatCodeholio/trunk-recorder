#include "xlat_channelizer.h"

xlat_channelizer::sptr xlat_channelizer::make(double input_rate, int samples_per_symbol, double symbol_rate, double center_freq, bool use_squelch) {

  return gnuradio::get_initial_sptr(new xlat_channelizer(input_rate, samples_per_symbol, symbol_rate, center_freq, use_squelch));
}

const int xlat_channelizer::smartnet_samples_per_symbol;
const int xlat_channelizer::phase1_samples_per_symbol;
const int xlat_channelizer::phase2_samples_per_symbol;
const double xlat_channelizer::phase1_symbol_rate;
const double xlat_channelizer::phase2_symbol_rate;
const double xlat_channelizer::smartnet_symbol_rate;

xlat_channelizer::DecimSettings xlat_channelizer::get_decim(long target_rate) {

  double t = target_rate;
  long if_freqs[] = {target_rate, 20000, 24000, 25000, 32000, 40000, 48000, 50000, 64000};
  xlat_channelizer::DecimSettings decim_settings = {-1, -1};

  for (size_t i = 0; i < sizeof(if_freqs) / sizeof(long); i++) {
    long if_freq = if_freqs[i];

    BOOST_LOG_TRIVIAL(debug) << "Trying if_freq: " << if_freq;

    if (if_freq < t) {
      BOOST_LOG_TRIVIAL(debug) << "rate too small" ;
      continue;
    }

    
    if  ((long(d_input_rate) % if_freq) != 0) {
      BOOST_LOG_TRIVIAL(debug) << "rate not divisible" ;
      continue;
    }
    
    long q = d_input_rate / if_freq;

    decim_settings.decim = q;

    BOOST_LOG_TRIVIAL(info) << "Recorder Decim: input_rate: " << d_input_rate << " target_rate: " << t << " output_rate: " << if_freq << " Decim: " << decim_settings.decim << " ARB: " << double(t) / if_freq;
    return decim_settings;
  }
  
  decim_settings.decim = floor(d_input_rate / t);
  double out_rate = d_input_rate / decim_settings.decim;

  BOOST_LOG_TRIVIAL(error) << "Recorder Decim: Nothing found using target_rate: " << t;
  BOOST_LOG_TRIVIAL(error) << "Recorder Decim: input_rate: " << d_input_rate << " target_rate: " << t << " output_rate: " << out_rate << " Decim: " << decim_settings.decim << " ARB: " << double(t) / (d_input_rate / decim_settings.decim);

  return decim_settings;
}

xlat_channelizer::xlat_channelizer(double input_rate, int samples_per_symbol, double symbol_rate, double center_freq, bool use_squelch)
    : gr::hier_block2("xlat_channelizer_ccf",
                      gr::io_signature::make(1, 1, sizeof(gr_complex)),
                      gr::io_signature::make(1, 1, sizeof(gr_complex))),
      d_center_freq(center_freq),
      d_input_rate(input_rate),
      d_samples_per_symbol(samples_per_symbol),
      d_symbol_rate(symbol_rate),
      d_use_squelch(use_squelch) {

  long channel_rate = d_symbol_rate * d_samples_per_symbol;
  // long if_rate = 12500;

  const float pi = M_PI;

  int decimation = floor(input_rate / channel_rate);
  xlat_channelizer::DecimSettings decim_settings = get_decim(input_rate);
  if (decim_settings.decim != -1) {
    decimation = decim_settings.decim;
  } 
  
  double resampled_rate = float(input_rate) / float(decimation);
  /*
      std::vector<float> if_coeffs;
  #if GNURADIO_VERSION < 0x030900

    if_coeffs = gr::filter::firdes::low_pass(1.0, input_rate, resampled_rate / 2, resampled_rate / 2, gr::filter::firdes::WIN_HAMMING);
  #else
    if_coeffs = gr::filter::firdes::low_pass(1.0, input_rate, resampled_rate / 2, resampled_rate / 2, gr::fft::window::WIN_HAMMING);
  #endif
  freq_xlat = gr::filter::freq_xlating_fir_filter<gr_complex, gr_complex, float>::make(decimation, if_coeffs, 0, input_rate); // inital_lpf_taps, 0, input_rate);
  */

  std::vector<gr_complex> if_coeffs;
  if_coeffs = gr::filter::firdes::complex_band_pass(1, input_rate, -channel_rate / 2, channel_rate / 2, channel_rate / 2);

  freq_xlat = make_freq_xlating_fft_filter(decimation, if_coeffs, 0, input_rate); // inital_lpf_taps, 0, input_rate);

  BOOST_LOG_TRIVIAL(info) << "\t Xlating Channelizer single-stage decimator - Decim: " << decimation << " Resampled Rate: " << resampled_rate << " Lowpass Size: " << if_coeffs.size();

  // ARB Resampler
  double arb_rate = channel_rate / resampled_rate;
  BOOST_LOG_TRIVIAL(info) << "\t Channelizer ARB - Symbol Rate: " << channel_rate << " Resampled Rate: " << resampled_rate << " ARB Rate: " << arb_rate;
  double arb_size = 32;
  double arb_atten = 30; // was originally 100
  // Create a filter that covers the full bandwidth of the output signal

  // If rate >= 1, we need to prevent images in the output,
  // so we have to filter it to less than half the channel
  // width of 0.5.  If rate < 1, we need to filter to less
  // than half the output signal's bw to avoid aliasing, so
  // the half-band here is 0.5*rate.
  double percent = 0.80;

  if (arb_rate < 1) {
    double halfband = 0.5 * arb_rate;
    double bw = percent * halfband;
    double tb = (percent / 2.0) * halfband;

// BOOST_LOG_TRIVIAL(info) << "Arb Rate: " << arb_rate << " Half band: " << halfband << " bw: " << bw << " tb: " <<
// tb;

// As we drop the bw factor, the optfir filter has a harder time converging;
// using the firdes method here for better results.
#if GNURADIO_VERSION < 0x030900
    arb_taps = gr::filter::firdes::low_pass_2(arb_size, arb_size, bw, tb, arb_atten, gr::filter::firdes::WIN_BLACKMAN_HARRIS);
#else
    arb_taps = gr::filter::firdes::low_pass_2(arb_size, arb_size, bw, tb, arb_atten, gr::fft::window::WIN_BLACKMAN_HARRIS);
#endif
    arb_resampler = gr::filter::pfb_arb_resampler_ccf::make(arb_rate, arb_taps);
  } else if (arb_rate > 1) {
    BOOST_LOG_TRIVIAL(error) << "Something is probably wrong! Resampling rate too low";
    exit(1);
  }
  

  double sps = d_samples_per_symbol;
  double def_excess_bw = 0.2;
  // Squelch DB
  // on a trunked network where you know you will have good signal, a carrier
  // power squelch works well. real FM receviers use a noise squelch, where
  // the received audio is high-passed above the cutoff and then fed to a
  // reverse squelch. If the power is then BELOW a threshold, open the squelch.

  squelch = gr::analog::pwr_squelch_cc::make(squelch_db, 0.0001, 0, true);

  rms_agc = gr::blocks::rms_agc::make(0.45, 0.85);
  fll_band_edge = gr::digital::fll_band_edge_cc::make(sps, def_excess_bw, 2 * sps + 1, (2.0 * pi) / sps / 250); // OP25 has this set to 350 instead of 250

  connect(self(), 0, freq_xlat, 0);

  if (d_use_squelch) {
    BOOST_LOG_TRIVIAL(info) << "Conventional - with Squelch";
    if (arb_rate == 1.0) {
      connect(freq_xlat, 0, squelch, 0);
    } else {
      connect(freq_xlat, 0, arb_resampler, 0);
      connect(arb_resampler, 0, squelch, 0);
    }
    connect(squelch, 0, rms_agc, 0);
  } else {
    if (arb_rate == 1.0) {
      connect(freq_xlat, 0, rms_agc, 0);
    } else {
      connect(freq_xlat, 0, arb_resampler, 0);
      connect(arb_resampler, 0, rms_agc, 0);
    }
  }

  connect(rms_agc, 0, fll_band_edge, 0);
  connect(fll_band_edge, 0, self(), 0);
}

int xlat_channelizer::get_freq_error() { // get frequency error from FLL and convert to Hz
  const float pi = M_PI;
  long if_rate = 24000;
  return int((fll_band_edge->get_frequency() / (2 * pi)) * if_rate);
}

bool xlat_channelizer::is_squelched() {
  return !squelch->unmuted();
}

double xlat_channelizer::get_pwr() {
  if (d_use_squelch) {
    return squelch->get_pwr();
  } else {
    return DB_UNSET;
  }
}


void xlat_channelizer::tune_offset(double f) {

  float freq = static_cast<float>(f);

  freq_xlat->set_center_freq(-freq);
}

void xlat_channelizer::set_squelch_db(double squelch_db) {
  squelch->set_threshold(squelch_db);
}

void xlat_channelizer::set_analog_squelch(bool analog_squelch) {
  if (analog_squelch) {
    squelch->set_alpha(0.01);
    squelch->set_ramp(10);
    squelch->set_gate(false);
  } else {
    squelch->set_alpha(0.0001);
    squelch->set_ramp(0);
    squelch->set_gate(true);
  }
}

void xlat_channelizer::set_samples_per_symbol(int samples_per_symbol) {
  fll_band_edge->set_samples_per_symbol(samples_per_symbol);
}