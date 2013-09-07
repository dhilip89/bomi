#ifndef AUDIOCONTROLLER_HPP
#define AUDIOCONTROLLER_HPP

#include "stdafx.hpp"

struct af_instance;		struct mp_audio;
struct af_cfg;			struct af_info;
enum class ClippingMethod;
struct NormalizerOption;

class AudioController : public QObject {
	Q_OBJECT
public:
	AudioController(QObject *parent = nullptr);
	~AudioController();
	void setLevel(double level);
	double level() const;
	bool setNormalizerActivated(bool on);
	bool setTempoScalerActivated(bool on);
	double gain() const;
	bool isTempoScalerActivated() const;
	bool isNormalizerActivated() const;
	void setNormalizerOption(double length, double target, double silence, double min, double max);
	double scale() const;
	const NormalizerOption &normalizerOption() const;
	void setClippingMethod(ClippingMethod method);
private:
	static int open(af_instance *af);
	int reinitialize(mp_audio *data);
	static mp_audio *play(af_instance *af, mp_audio *data);
	static void uninit(af_instance *af);
	static int control(af_instance *af, int cmd, void *arg);
	struct Data;
	Data *d;
	friend af_info create_info();
};

#endif // AUDIOCONTROLLER_HPP
