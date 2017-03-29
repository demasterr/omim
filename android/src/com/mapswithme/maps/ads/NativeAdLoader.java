package com.mapswithme.maps.ads;

import android.content.Context;
import android.support.annotation.NonNull;
import android.support.annotation.Nullable;

public interface NativeAdLoader
{
  /**
   * Loads an ad for the specified banner id. A caller will be notified about loading through
   * {@link NativeAdListener} interface.
   *
   * @param context An activity context.
   * @param bannerId A banner id that ad will be loaded for.
   */
  void loadAd(@NonNull Context context, @NonNull String bannerId);

  /**
   * Caller should set this listener to be informed about status of an ad loading.
   *
   * @see NativeAdListener
   */
  void setAdListener(@Nullable NativeAdListener adListener);

  /**
   * Indicated whether the ad for the specified banner is loading right now or not.
   * @param bannerId A specified banner id.
   * @return <code>true</code> if loading is in a progress, otherwise - <code>false</code>.
   */
  boolean isAdLoading(@NonNull String bannerId);
}
