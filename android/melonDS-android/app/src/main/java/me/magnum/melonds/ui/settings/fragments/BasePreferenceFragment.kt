package me.magnum.melonds.ui.settings.fragments

import android.animation.AnimatorInflater
import android.graphics.Rect
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.RelativeLayout
import android.widget.TextView
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceGroupAdapter
import androidx.preference.PreferenceScreen
import androidx.preference.PreferenceViewHolder
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import me.magnum.melonds.R

abstract class BasePreferenceFragment : PreferenceFragmentCompat() {

    /** Two columns of compact cards on the Thor's landscape top screen, so that most pages fit without scrolling. */
    protected open fun columnCount(widthDp: Int) = if (widthDp >= 600) 2 else 1

    protected open fun spanSize(position: Int, itemCount: Int, columns: Int) = 1

    override fun onCreateAdapter(preferenceScreen: PreferenceScreen): RecyclerView.Adapter<*> = SettingsCardAdapter(preferenceScreen)

    override fun onCreateRecyclerView(inflater: LayoutInflater, parent: ViewGroup, savedInstanceState: Bundle?): RecyclerView {
        return super.onCreateRecyclerView(inflater, parent, savedInstanceState).apply {
            applySettingsCardGrid(columnCount(resources.configuration.screenWidthDp), ::spanSize)
        }
    }
}

/** Shows each preference as a compact card in the settings palette. Categories keep a plain background. */
class SettingsCardAdapter(preferenceScreen: PreferenceScreen) : PreferenceGroupAdapter(preferenceScreen) {
    override fun onBindViewHolder(holder: PreferenceViewHolder, position: Int) {
        super.onBindViewHolder(holder, position)
        val isCategory = getItem(position) is PreferenceCategory
        holder.itemView.apply {
            val dp = resources.displayMetrics.density
            if (isCategory) {
                background = null
                elevation = 0f
                stateListAnimator = null
            } else {
                setBackgroundResource(R.drawable.settings_card)
                elevation = 2 * dp
                minimumHeight = (60 * dp).toInt()
                stateListAnimator = AnimatorInflater.loadStateListAnimator(context, R.animator.settings_card_lift)
                (findViewById<View>(android.R.id.title) as? TextView)?.apply {
                    textSize = 15f
                    // The default preference layout pads its text block by 16dp above and below
                    (parent as? RelativeLayout)?.takeIf { it.paddingTop > 0 }?.updatePadding(top = (10 * dp).toInt(), bottom = (10 * dp).toInt())
                }
                (findViewById<View>(android.R.id.summary) as? TextView)?.textSize = 13f
            }
        }
        holder.isDividerAllowedAbove = false
        holder.isDividerAllowedBelow = false
    }
}

/** Lays the preference cards out in [columns] columns, with categories spanning the full width. */
fun RecyclerView.applySettingsCardGrid(columns: Int, spanSize: (position: Int, itemCount: Int, columns: Int) -> Int) {
    val density = resources.displayMetrics.density
    fun dp(value: Int) = (value * density).toInt()
    val gap = dp(10)
    clipToPadding = false
    setPadding(dp(20), dp(if (resources.configuration.screenWidthDp >= 600) 4 else 8), dp(20), dp(20))
    layoutManager = GridLayoutManager(context, columns).apply {
        spanSizeLookup = object : GridLayoutManager.SpanSizeLookup() {
            override fun getSpanSize(position: Int): Int {
                val adapter = adapter as? PreferenceGroupAdapter ?: return 1
                return if (adapter.getItem(position) is PreferenceCategory) columns else spanSize(position, adapter.itemCount, columns)
            }
        }
    }
    addItemDecoration(object : RecyclerView.ItemDecoration() {
        override fun getItemOffsets(outRect: Rect, view: View, parent: RecyclerView, state: RecyclerView.State) {
            val params = view.layoutParams as GridLayoutManager.LayoutParams
            val start = params.spanIndex * gap / columns
            val end = gap - (params.spanIndex + params.spanSize) * gap / columns
            if (parent.layoutDirection == View.LAYOUT_DIRECTION_RTL) {
                outRect.set(end, 0, start, gap)
            } else {
                outRect.set(start, 0, end, gap)
            }
        }
    })
    ViewCompat.setOnApplyWindowInsetsListener(this) { view, windowInsets ->
        val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
        view.updatePadding(bottom = insets.bottom + dp(20))
        WindowInsetsCompat.CONSUMED
    }
}
