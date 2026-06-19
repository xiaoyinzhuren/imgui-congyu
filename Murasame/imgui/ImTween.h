#pragma once
#include <vector>
#include <tuple>
#include <type_traits>
#include <functional>

template <typename T = float>
class ImTween
{
public:
    enum TweenType
    {
        PingPong
    };

private:
    TweenType type;
    float speed = 0.5f;
    std::function<void()> onComplete;
    bool hasOnComplete = false;
    std::function<bool()> condition;

    std::vector<std::tuple<T, T, T*>> tweens;

    ImTween(std::vector<std::tuple<T, T, T*>>&& tweens)
    {
        this->tweens = std::move(tweens);
    }

public:
    /*
     * accepts unlimited number of values in form of std::tuple<T, T, T*> { min, max, value }
     */
    // 递归辅助函数
    template <typename First, typename... Rest>
    static void AddTweens(std::vector<std::tuple<T, T, T*>>& tweens, First first, Rest... rest)
    {
        tweens.push_back(first);
        AddTweens(tweens, rest...);
    }

    // 递归终止函数
    static void AddTweens(std::vector<std::tuple<T, T, T*>>& tweens)
    {
    }

    template <typename... Values>
    static ImTween Tween(Values... values)
    {
        std::vector<std::tuple<T, T, T*>> tweens;
        AddTweens(tweens, values...);
        return ImTween(std::move(tweens));
    }

    ImTween& OfType(TweenType type)
    {
        this->type = type;
        return *this;
    }

    ImTween& Speed(float speed)
    {
        this->speed = speed;
        return *this;
    }

    ImTween& When(std::function<bool()> condition)
    {
        this->condition = condition;
        return *this;
    }

    ImTween& OnComplete(std::function<void()> onComplete)
    {
        this->onComplete = onComplete;
        this->hasOnComplete = true;
        return *this;
    }

    void Tick()
    {
        for (auto& tween : tweens)
        {
            T min = std::get<0>(tween);
            T max = std::get<1>(tween);
            T& value = *std::get<2>(tween);

            if (this->condition() && value < max)
            {
                value += speed;

                if (value > max)
                    value = max;
            }
            else if ((this->type == PingPong) && !this->condition() && value > min)
            {
                value -= speed;

                if (value < min)
                    value = min;
            }

            if (this->hasOnComplete && value == max)
                this->onComplete();
        }
    }
};