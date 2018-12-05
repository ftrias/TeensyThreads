#ifndef RUNNABLE_H
#define RUNNABLE_H

/*
 * File Purpose
 *    This is an abstract class that is reusable to allow for easy defintion of a runnable function for std::thread
 */

class Runnable{
private:
protected:
	virtual void runTarget(void *arg) = 0;
public:
	virtual ~Runnable(){}

	static void runThread(void *arg)
	{
		Runnable *_runnable = static_cast<Runnable*> (arg);
		_runnable->runTarget(arg);
	}
};

#endif // RUNNABLE_H
