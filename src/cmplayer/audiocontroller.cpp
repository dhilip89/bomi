#include "audiocontroller.hpp"
#include "audiofilter.hpp"
#include "enums.hpp"

af_info create_info();
af_info af_info_dummy = create_info();
struct cmplayer_af_priv { AudioController *ac; char *address; };
static AudioController *priv(af_instance *af) { return static_cast<cmplayer_af_priv*>(af->priv)->ac; }

struct AudioController::Data {
	struct BufferInfo {
		BufferInfo(int samples = 0): samples(samples) {}
		void reset() {level = 0.0; samples = 0;}
		double level = 0.0; int samples = 0;
	};

	bool normalizerActivated = false;
	bool tempoScalerActivated = false;
	bool volumeChanged = false;
	double scale = 1.0;
	TempoScaler *scaler = nullptr;
	VolumeController *volume = nullptr;

	mp_audio data;
	af_instance *af = nullptr;
	float level[AF_NCH];

	NormalizerOption normalizerOption;

	ClippingMethod clip = ClippingMethod::Auto;
};

AudioController::AudioController(QObject *parent): QObject(parent), d(new Data) {
	std::fill_n(d->level, AF_NCH, 1.0);
	memset(&d->data, 0, sizeof(d->data));
}

AudioController::~AudioController() {
	delete d->scaler;
	delete d->volume;
	delete d;
}

void AudioController::setClippingMethod(ClippingMethod method) {
	d->clip = method;
}

int AudioController::open(af_instance *af) {
	auto priv = static_cast<cmplayer_af_priv*>(af->priv);
	priv->ac = address_cast<AudioController*>(priv->address);
	auto d = priv->ac->d;
	d->af = af;

	af->control = AudioController::control;
	af->uninit = AudioController::uninit;
	af->play = AudioController::play;
	af->mul = 1;
	af->setup = nullptr;
	af->data = &d->data;


	return AF_OK;
}

void AudioController::uninit(af_instance *af) {
	free(af->data->audio);
	memset(af->data, 0, sizeof(d->data));
	auto ac = priv(af);
	if (ac)
		ac->d->af = nullptr;
}

template<typename Filter>
Filter *check(Filter *&filter, const mp_audio &data) {
	if (!filter || !filter->isCompatibleWith(&data)) {
		delete filter;
		filter = Filter::create(data.format);
	}
	if (filter)
		filter->reconfigure(&data);
	return filter;
}

VolumeController *check(VolumeController *&filter, ClippingMethod clip, const mp_audio &data) {
	if (!filter || !filter->isCompatibleWith(&data) || filter->clippingMethod() != clip) {
		delete filter;
		filter = VolumeController::create(data.format, clip);
	}
	if (filter)
		filter->reconfigure(&data);
	return filter;
}

int AudioController::reinitialize(mp_audio *data) {
	d->volumeChanged = false;
	if (!data)
		return AF_ERROR;
	mp_audio_copy_config(&d->data, data);
	switch (data->format) {
	case AF_FORMAT_S16_NE:
	case AF_FORMAT_S32_NE:
	case AF_FORMAT_FLOAT_NE:
	case AF_FORMAT_DOUBLE_NE:
		break;
	default:
		mp_audio_set_format(&d->data, AF_FORMAT_FLOAT_NE);
	}
	check(d->volume, d->clip, d->data);
	check(d->scaler, d->data);
	return af_test_output(d->af, data);
}

void AudioController::setLevel(double level) {
	std::fill_n(d->level, AF_NCH, level);
}

double AudioController::level() const {
	return d->level[0];
}

int AudioController::control(af_instance *af, int cmd, void *arg) {
	auto ac = priv(af);
	auto d = ac->d;
	switch(cmd){
	case AF_CONTROL_REINIT:
		return ac->reinitialize(static_cast<mp_audio*>(arg));
	case AF_CONTROL_VOLUME_LEVEL | AF_CONTROL_SET:
		d->volumeChanged = true;
		return af_from_dB(AF_NCH, (float*)arg, d->level, 20.0, -200.0, 60.0);
	case AF_CONTROL_VOLUME_LEVEL | AF_CONTROL_GET:
		return af_to_dB(AF_NCH, d->level, (float*)arg, 20.0);
	case AF_CONTROL_PLAYBACK_SPEED | AF_CONTROL_SET:
	case AF_CONTROL_SCALETEMPO_AMOUNT | AF_CONTROL_SET:
		d->scale = *(double*)arg;
	case AF_CONTROL_SCALETEMPO_AMOUNT | AF_CONTROL_GET:
		*(double*)arg = d->scale;
		return d->tempoScalerActivated ? AF_OK : AF_UNKNOWN;
	default:
		return AF_UNKNOWN;
	}
}

mp_audio *AudioController::play(af_instance *af, mp_audio *data) {
	auto ac = priv(af); auto d = ac->d;
	if (!d->volumeChanged && !d->normalizerActivated && (!d->tempoScalerActivated || d->scale == 1.0))
		return data;
	af->mul = 1.0;
	af->delay = 0.0;
	if (d->volume && d->volume->prepare(ac, data))
		data = d->volume->play(data);
	if (d->scaler && d->scaler->prepare(ac, data)) {
		data = d->scaler->play(data);
		af->mul = d->scaler->multiplier();
		af->delay = d->scaler->delay();
	}
	return data;
}

bool AudioController::setNormalizerActivated(bool on) {
	return _Change(d->normalizerActivated, on);
}

bool AudioController::setTempoScalerActivated(bool on) {
	return _Change(d->tempoScalerActivated, on);
}

double AudioController::gain() const {
	return d->normalizerActivated && d->volume ? d->volume->gain() : 1.0;
}

bool AudioController::isTempoScalerActivated() const {
	return d->tempoScalerActivated;
}

double AudioController::scale() const {
	return d->scale;
}

void AudioController::setNormalizerOption(double length, double target, double silence, double min, double max) {
	d->normalizerOption.bufferLengthInSeconds = length;
	d->normalizerOption.targetLevel = target;
	d->normalizerOption.silenceLevel = silence;
	d->normalizerOption.minimumGain = min;
	d->normalizerOption.maximumGain = max;
}

bool AudioController::isNormalizerActivated() const {
	return d->normalizerActivated;
}

const NormalizerOption &AudioController::normalizerOption() const {
	return d->normalizerOption;
}

af_info create_info() {
	static m_option options[2];
	memset(options, 0, sizeof(options));
	options[0].name = "address";
	options[0].flags = 0;
	options[0].defval = 0;
	options[0].offset = MP_CHECKED_OFFSETOF(cmplayer_af_priv, address, char*);
	options[0].is_new_option = 1;
	options[0].type = &m_option_type_string;

	static af_info info = {
		"CMPlayer audio controller",
		"dummy",
		"xylosper",
		"",
		AF_FLAGS_NOT_REENTRANT,
		AudioController::open,
		nullptr,
		sizeof(cmplayer_af_priv),
		nullptr,
		options
	};
	return info;
}
